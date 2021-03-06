//-----------------------------------------------------------------------------
// HIMG, by Marcus Geelnard, 2015
//
// This is free and unencumbered software released into the public domain.
//
// See LICENSE for details.
//-----------------------------------------------------------------------------

#include "decoder.h"

#include <algorithm>
#include <iostream>

#include "common.h"
#include "downsampled.h"
#include "hadamard.h"
#include "huffman.h"
#include "quantize.h"

namespace himg {

namespace {

uint32_t ToFourcc(const char name[4]) {
  return static_cast<uint32_t>(name[0]) |
         (static_cast<uint32_t>(name[1]) << 8) |
         (static_cast<uint32_t>(name[2]) << 16) |
         (static_cast<uint32_t>(name[3]) << 24);
}

uint8_t ClampTo8Bit(int16_t x) {
  return x >= 0 ? (x <= 255 ? static_cast<uint8_t>(x) : 255) : 0;
}

void RestoreChannelBlock(uint8_t *out,
                         const int16_t *in,
                         int pixel_stride,
                         int row_stride,
                         int block_width,
                         int block_height) {
  for (int y = 0; y < block_height; y++) {
    for (int x = 0; x < block_width; x++) {
      *out = ClampTo8Bit(*in++);
      out += pixel_stride;
    }
    in += 8 - block_width;
    out += row_stride - (pixel_stride * block_width);
  }
}

void YCbCrToRGB(uint8_t *buf,
                int width,
                int height,
                int num_channels) {
  // We use a multiplier-less approximation:
  //   Y  = (R + 2G + B) / 4
  //   Cb = B - G
  //   Cr = R - G
  //
  //   G = Y - (Cb + Cr) / 4
  //   B = G + Cb
  //   R = G + Cr
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Convert YCbCr -> RGB.
      int16_t y = static_cast<int16_t>(buf[0]);
      int16_t cb = (static_cast<int16_t>(buf[1]) << 1) - 255;
      int16_t cr = (static_cast<int16_t>(buf[2]) << 1) - 255;
      int16_t g = y - ((cb + cr + 2) >> 2);
      int16_t b = g + cb;
      int16_t r = g + cr;
      buf[0] = ClampTo8Bit(r);
      buf[1] = ClampTo8Bit(g);
      buf[2] = ClampTo8Bit(b);

      // Note: Remaining channels are kept as-is (e.g. alpha).

      buf += num_channels;
    }
  }
}

}  // namespace

Decoder::Decoder() {
}

bool Decoder::Decode(const uint8_t *packed_data, int packed_size) {
  m_packed_data = packed_data;
  m_packed_size = packed_size;
  m_packed_idx = 0;

  m_unpacked_data.clear();

  // Check that this is a RIFF HIMG file.
  if (!DecodeRIFFStart()) {
    std::cout << "Not a RIFF HIMG file.\n";
    return false;
  }

  // Header data.
  if (!DecodeHeader()) {
    std::cout << "Error decoding header.\n";
    return false;
  }

  // Quantization table.
  if (!DecodeQuantizationConfig()) {
    std::cout << "Error decoding quantization configuration.\n";
    return false;
  }

  // Lowres data.
  if (!DecodeLowRes()) {
    std::cout << "Error decoding low-res data.\n";
    return false;
  }

  // Full resolution data.
  if (!DecodeFullRes()) {
    std::cout << "Error decoding full-res data.\n";
    return false;
  }

  return true;
}

bool Decoder::DecodeRIFFStart() {
  if (m_packed_size < 12)
    return false;

  if (m_packed_data[0] != 'R' || m_packed_data[1] != 'I' ||
      m_packed_data[2] != 'F' || m_packed_data[3] != 'F')
    return false;

  int file_size = static_cast<int>(m_packed_data[4]) |
                  (static_cast<int>(m_packed_data[5]) << 8) |
                  (static_cast<int>(m_packed_data[6]) << 16) |
                  (static_cast<int>(m_packed_data[7]) << 24);
  if (file_size + 8 != m_packed_size)
    return false;

  if (m_packed_data[8] != 'H' || m_packed_data[9] != 'I' ||
      m_packed_data[10] != 'M' || m_packed_data[11] != 'G')
    return false;

  m_packed_idx += 12;

  return true;
}

bool Decoder::DecodeHeader() {
  // Find the FRMT chunk.
  int chunk_size;
  if (!FindRIFFChunk(ToFourcc("FRMT"), &chunk_size))
    return false;
  const uint8_t *chunk_data = &m_packed_data[m_packed_idx];
  m_packed_idx += chunk_size;

  // Check the header size.
  if (chunk_size < 11)
    return false;

  // Check version.
  uint8_t version = chunk_data[0];
  if (version != 1) {
    std::cout << "Incorrect HIMG version number.\n";
    return false;
  }

  // Get image dimensions.
  m_width = static_cast<int>(chunk_data[1]) |
            (static_cast<int>(chunk_data[2]) << 8) |
            (static_cast<int>(chunk_data[3]) << 16) |
            (static_cast<int>(chunk_data[4]) << 24);
  m_height = static_cast<int>(chunk_data[5]) |
             (static_cast<int>(chunk_data[6]) << 8) |
             (static_cast<int>(chunk_data[7]) << 16) |
             (static_cast<int>(chunk_data[8]) << 24);
  m_num_channels = static_cast<int>(chunk_data[9]);
  m_use_ycbcr = chunk_data[10] != 0;

  std::cout << "Image dimensions: " << m_width << "x" << m_height << " ("
            << m_num_channels << " channels).\n";

  return true;
}

bool Decoder::DecodeQuantizationConfig() {
  // Find the QCFG chunk.
  int chunk_size;
  if (!FindRIFFChunk(ToFourcc("QCFG"), &chunk_size))
    return false;
  const uint8_t *chunk_data = &m_packed_data[m_packed_idx];
  m_packed_idx += chunk_size;

  // Restore the configuration.
  return m_quantize.SetConfiguration(chunk_data, chunk_size);
}

bool Decoder::DecodeLowRes() {
  // Find the LRES chunk.
  int chunk_size;
  if (!FindRIFFChunk(ToFourcc("LRES"), &chunk_size))
    return false;

  // Prepare a buffer for all channels.
  const int num_rows = (m_height + 7) >> 3;
  const int num_cols = (m_width + 7) >> 3;
  const int num_blocks = num_rows * num_cols;
  const int channel_size = num_blocks;
  const int unpacked_size = channel_size * m_num_channels;
  std::vector<uint8_t> unpacked_data(unpacked_size);

  // Uncompress source data.
  if (!UncompressData(unpacked_data.data(), unpacked_size, chunk_size))
    return false;

  // Initialize the downsampled version of each channel.
  for (int chan = 0; chan < m_num_channels; ++chan) {
    m_downsampled.push_back(Downsampled());
    Downsampled &downsampled = m_downsampled.back();
    downsampled.SetBlockData(
        unpacked_data.data() + channel_size * chan, num_rows, num_cols);
  }

  std::cout << "Decoded lowres data " << num_cols << "x" << num_rows << std::endl;

  return true;
}

bool Decoder::DecodeFullRes() {
  // Find the FRES chunk.
  int chunk_size;
  if (!FindRIFFChunk(ToFourcc("FRES"), &chunk_size))
    return false;

  // Reserve space for the output data.
  m_unpacked_data.resize(m_width * m_height * m_num_channels);

  // Prepare an unpacked buffer for all channels.
  const int num_blocks = ((m_width + 7) >> 3) * ((m_height + 7) >> 3);
  const int unpacked_size = num_blocks * 64 * m_num_channels;
  std::vector<uint8_t> unpacked_data(unpacked_size);

  // Uncompress all channels using Huffman.
  if (!UncompressData(unpacked_data.data(), unpacked_size, chunk_size))
    return false;
  std::cout << "Uncompressed full res data.\n";

  // Process all the 8x8 blocks.
  int unpacked_idx = 0;
  for (int y = 0; y < m_height; y += 8) {
    // Vertical block coordinate (v).
    int v = y >> 3;
    int block_height = std::min(8, m_height - y);

    // TODO(m): Do Huffman decompression of a single block row at a time.

    // All channels are inteleaved per block row.
    for (int chan = 0; chan < m_num_channels; ++chan) {
      // Get the low-res (divided by 8x8) image for this channel.
      Downsampled &downsampled = m_downsampled[chan];

      bool is_chroma_channel = m_use_ycbcr && (chan == 1 || chan == 2);

      for (int x = 0; x < m_width; x += 8) {
        // Horizontal block coordinate (u).
        int u = x >> 3;
        int block_width = std::min(8, m_width - x);

        // Get quantized data from the unpacked buffer.
        uint8_t packed[64];
        for (int i = 0; i < 64; ++i) {
          packed[kIndexLUT[i]] =
              unpacked_data[unpacked_idx + u + i * downsampled.columns()];
        }

        // De-quantize.
        int16_t buf1[64];
        m_quantize.Unpack(buf1, packed, is_chroma_channel);

        // Inverse transform.
        int16_t buf0[64];
        Hadamard::Inverse(buf0, buf1);

        // Add low-res component.
        int16_t lowres[64];
        downsampled.GetLowresBlock(lowres, u, v);
        for (int i = 0; i < 64; ++i) {
          buf0[i] += lowres[i];
        }

        // Copy color channel to destination data.
        RestoreChannelBlock(
            &m_unpacked_data[(y * m_width + x) * m_num_channels + chan],
            buf0,
            m_num_channels,
            m_width * m_num_channels,
            block_width,
            block_height);
      }

      unpacked_idx += downsampled.columns() * 64;
    }

    // Do YCbCr->RGB conversion for this block row if necessary.
    if (m_use_ycbcr) {
      uint8_t *buf = &m_unpacked_data[y * m_width * m_num_channels];
      YCbCrToRGB(buf, m_width, block_height, m_num_channels);
    }
  }

  return true;
}

bool Decoder::DecodeRIFFChunk(uint32_t *fourcc, int *size) {
  if ((m_packed_idx + 8) > m_packed_size)
    return false;

  *fourcc = static_cast<uint32_t>(m_packed_data[m_packed_idx]) |
            (static_cast<uint32_t>(m_packed_data[m_packed_idx + 1]) << 8) |
            (static_cast<uint32_t>(m_packed_data[m_packed_idx + 2]) << 16) |
            (static_cast<uint32_t>(m_packed_data[m_packed_idx + 3]) << 24);
  *size = static_cast<int>(m_packed_data[m_packed_idx + 4]) |
          (static_cast<int>(m_packed_data[m_packed_idx + 5]) << 8) |
          (static_cast<int>(m_packed_data[m_packed_idx + 6]) << 16) |
          (static_cast<int>(m_packed_data[m_packed_idx + 7]) << 24);

  m_packed_idx += 8;
  return m_packed_idx + *size <= m_packed_size;
}

bool Decoder::FindRIFFChunk(uint32_t fourcc, int *size) {
  uint32_t chunk_fourcc;
  int chunk_size;
  while (DecodeRIFFChunk(&chunk_fourcc, &chunk_size)) {
    // Did we find the requested chunk?
    if (chunk_fourcc == fourcc) {
      *size = chunk_size;
      return true;
    }

    // Unrecognized chunk. Skip to the next one.
    m_packed_idx += chunk_size;
  }

  // We didn't find the chunk.
  return false;
}

bool Decoder::UncompressData(uint8_t *out, int out_size, int in_size) {
  // Uncompress data.
  std::cout << "Unpacking " << in_size << " Huffman compressed bytes."
            << std::endl;
  Huffman::Uncompress(out, m_packed_data + m_packed_idx, in_size, out_size);
  m_packed_idx += in_size;

  return true;
}

}  // namespace himg
