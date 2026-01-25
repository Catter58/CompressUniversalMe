/**
 * CompressUM - Original Compression Algorithm
 * CRC32-C implementation
 */

#include "compressum/core/crc32.hpp"

namespace compressum::core {

/**
 * GF(2) matrix multiplication for CRC combination
 */
static uint32_t gf2_matrix_times(const uint32_t* matrix, uint32_t vec) {
    uint32_t sum = 0;
    while (vec) {
        if (vec & 1) {
            sum ^= *matrix;
        }
        vec >>= 1;
        ++matrix;
    }
    return sum;
}

/**
 * Square a GF(2) matrix
 */
static void gf2_matrix_square(uint32_t* square, const uint32_t* matrix) {
    for (int n = 0; n < 32; ++n) {
        square[n] = gf2_matrix_times(matrix, matrix[n]);
    }
}

/**
 * Combine two CRC32-C values
 *
 * Given CRC(A) and CRC(B) where B has length len2,
 * compute CRC(A || B) where || is concatenation.
 *
 * Uses the matrix exponentiation method:
 * CRC(A || B) = CRC(A) * M^len2 + CRC(B)
 * where M is the CRC state transition matrix.
 */
uint32_t CRC32::combine(uint32_t crc1, uint32_t crc2, size_t len2) {
    if (len2 == 0) {
        return crc1;
    }

    // CRC32-C polynomial in reflected form
    constexpr uint32_t POLY = 0x82F63B78u;

    // Build matrix for single zero bit
    uint32_t even[32];  // even power-of-two zeros operator
    uint32_t odd[32];   // odd power-of-two zeros operator

    // Put operator for one zero bit in odd
    odd[0] = POLY;
    uint32_t row = 1;
    for (int n = 1; n < 32; ++n) {
        odd[n] = row;
        row <<= 1;
    }

    // Put operator for two zero bits in even
    gf2_matrix_square(even, odd);

    // Put operator for four zero bits in odd
    gf2_matrix_square(odd, even);

    // Apply len2 zeros to crc1
    do {
        // Apply zeros operator for this bit of len2
        gf2_matrix_square(even, odd);
        if (len2 & 1) {
            crc1 = gf2_matrix_times(even, crc1);
        }
        len2 >>= 1;

        // If no more bits set, done
        if (len2 == 0) {
            break;
        }

        // Another iteration of the loop with even and odd swapped
        gf2_matrix_square(odd, even);
        if (len2 & 1) {
            crc1 = gf2_matrix_times(odd, crc1);
        }
        len2 >>= 1;
    } while (len2 != 0);

    // Combine CRCs
    crc1 ^= crc2;
    return crc1;
}

} // namespace compressum::core
