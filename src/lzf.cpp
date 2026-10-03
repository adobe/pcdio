/*
 * Copyright 2026 Adobe. All rights reserved.
 * This file is licensed to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
 * OF ANY KIND, either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */

//
// The LZF stream format and the compression/decompression routines below are derived from liblzf.
// This matches the format produced/consumed by PCL's binary_compressed PCD files. The original
// license is retained verbatim below as required by its redistribution conditions:
//
//   Copyright (c) 2000-2010 Marc Alexander Lehmann <schmorp@schmorp.de>
//
//   Redistribution and use in source and binary forms, with or without modifica-
//   tion, are permitted provided that the following conditions are met:
//
//     1.  Redistributions of source code must retain the above copyright notice,
//         this list of conditions and the following disclaimer.
//
//     2.  Redistributions in binary form must reproduce the above copyright
//         notice, this list of conditions and the following disclaimer in the
//         documentation and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
//   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANT-
//   ABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
//   THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
//   OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUB-
//   STITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTER-
//   RUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
//   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
//   WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
//   SUCH DAMAGE.
//

#include "lzf.h"

#include <cstdint>
#include <vector>

namespace pcdio::internal {

namespace {

using u8 = uint8_t;

constexpr unsigned HLOG = 16;
constexpr unsigned HSIZE = 1u << HLOG;
constexpr unsigned MAX_LIT = 1u << 5;               // 32
constexpr unsigned MAX_OFF = 1u << 13;              // 8192
constexpr unsigned MAX_REF = (1u << 8) + (1u << 3); // 264

inline unsigned first_hash(const u8* p)
{
    return (unsigned(p[0]) << 8) | p[1];
}

inline unsigned next_hash(unsigned v, const u8* p)
{
    return (v << 8) | p[2];
}

inline unsigned hash_index(unsigned h)
{
    return (((h ^ (h << 5)) >> (3 * 8 - HLOG)) - h * 5) & (HSIZE - 1);
}

} // namespace

size_t lzf_compress(const void* in_data, size_t in_len_bytes, void* out_data, size_t out_len_bytes)
{
    // Keep lengths in size_t: the format permits payloads up to 4 GiB, and a worst-case output
    // capacity (input * 17/16 + 64) exceeds 32 bits before the 4 GiB input limit is reached.
    const size_t in_len = in_len_bytes;
    const size_t out_len = out_len_bytes;
    if (in_len == 0 || out_len == 0) return 0;

    std::vector<const u8*> htab(HSIZE, nullptr);

    const u8* ip = static_cast<const u8*>(in_data);
    u8* op = static_cast<u8*>(out_data);
    const u8* in_end = ip + in_len;
    u8* out_end = op + out_len;

    int lit = 0;
    op++; // reserve control byte for the initial literal run

    // first_hash() reads two bytes; only prime it when at least two bytes are available.
    unsigned hval = in_len >= 2 ? first_hash(ip) : 0;
    while (in_len > 2 && ip < in_end - 2) {
        hval = next_hash(hval, ip);
        const unsigned hslot = hash_index(hval);
        const u8* ref = htab[hslot];
        htab[hslot] = ip;

        size_t off;
        if (ref && ref < ip && (off = static_cast<size_t>(ip - ref - 1)) < MAX_OFF &&
            ref > static_cast<const u8*>(in_data) && ref[2] == ip[2] && ref[1] == ip[1] &&
            ref[0] == ip[0]) {
            // Match found: extend it up to MAX_REF octets.
            unsigned len = 2;
            size_t maxlen = static_cast<size_t>(in_end - ip) - len;
            maxlen = maxlen > MAX_REF ? MAX_REF : maxlen;

            // Remaining-capacity comparisons; forming `op + 4` beyond the buffer is UB.
            if (static_cast<size_t>(out_end - op) <= 4) { // fast conservative test
                if (static_cast<size_t>(out_end - op) + static_cast<size_t>(!lit) <= 4) {
                    return 0; // exact test
                }
            }

            op[-lit - 1] = static_cast<u8>(lit - 1); // stop the current literal run
            op -= (lit == 0);                        // undo the reserved byte if the run was empty

            do {
                len++;
            } while (len < maxlen && ref[len] == ip[len]);

            len -= 2; // len is now (#octets - 2), i.e. the value stored in the stream
            ip++;

            if (len < 7) {
                *op++ = static_cast<u8>((off >> 8) + (len << 5));
            } else {
                *op++ = static_cast<u8>((off >> 8) + (7 << 5));
                *op++ = static_cast<u8>(len - 7);
            }
            *op++ = static_cast<u8>(off);

            lit = 0;
            op++; // reserve control byte for the next literal run

            ip += len + 1;

            if (ip >= in_end - 2) break;

            // Re-insert hash entries for the bytes we skipped over.
            ip -= len + 1;
            do {
                hval = next_hash(hval, ip);
                htab[hash_index(hval)] = ip;
                ip++;
            } while (len--);
        } else {
            // One more literal byte.
            if (op >= out_end) return 0;
            lit++;
            *op++ = *ip++;
            if (lit == static_cast<int>(MAX_LIT)) {
                op[-lit - 1] = static_cast<u8>(lit - 1); // stop run
                lit = 0;
                // The reserve must keep op <= out_end: the capacity tests compute
                // (out_end - op) and a wrapped negative difference would defeat them.
                if (op >= out_end) return 0;
                op++; // start run
            }
        }
    }

    if (static_cast<size_t>(out_end - op) < 3) return 0; // at most 3 bytes can be missing here

    while (ip < in_end) {
        lit++;
        *op++ = *ip++;
        if (lit == static_cast<int>(MAX_LIT)) {
            op[-lit - 1] = static_cast<u8>(lit - 1); // stop run
            lit = 0;
            // Same op <= out_end invariant as the main-loop flush.
            if (op >= out_end) return 0;
            op++; // start run
        }
    }

    op[-lit - 1] = static_cast<u8>(lit - 1); // end the final run
    op -= (lit == 0);                        // undo the reserved byte if the run was empty

    return static_cast<size_t>(op - static_cast<u8*>(out_data));
}

size_t lzf_decompress(
    const void* in_data, size_t in_len_bytes, void* out_data, size_t out_len_bytes)
{
    if (in_len_bytes == 0) return 0;

    const u8* ip = static_cast<const u8*>(in_data);
    u8* op = static_cast<u8*>(out_data);
    const u8* const in_end = ip + in_len_bytes;
    u8* const out_end = op + out_len_bytes;

    do {
        unsigned ctrl = *ip++;

        if (ctrl < (1u << 5)) {
            // Literal run of (ctrl + 1) bytes.  Compare against the remaining capacity instead
            // of forming `op + ctrl`: pointer arithmetic beyond one-past the buffer is UB even
            // when used only for comparison.
            ctrl++;
            if (ctrl > static_cast<size_t>(out_end - op)) return 0;
            if (ctrl > static_cast<size_t>(in_end - ip)) return 0;
            do {
                *op++ = *ip++;
            } while (--ctrl);
        } else {
            // Back reference. Compute the numeric offset and validate it against the bytes already
            // produced before forming the pointer (forming an out-of-bounds pointer is UB).
            unsigned len = ctrl >> 5;
            if (len == 7) {
                if (ip >= in_end) return 0;
                len += *ip++;
            }
            if (ip >= in_end) return 0;
            const size_t off = (static_cast<size_t>(ctrl & 0x1f) << 8) + *ip++ + 1;
            const size_t produced = static_cast<size_t>(op - static_cast<u8*>(out_data));
            if (off > produced) return 0; // reference would point before the output buffer
            u8* ref = op - off;

            // Same UB-avoidance as above: `len + 2` cannot overflow (len <= 262).
            if (static_cast<size_t>(len) + 2 > static_cast<size_t>(out_end - op)) return 0;

            *op++ = *ref++;
            *op++ = *ref++;
            do {
                *op++ = *ref++;
            } while (--len);
        }
    } while (ip < in_end);

    return static_cast<size_t>(op - static_cast<u8*>(out_data));
}

} // namespace pcdio::internal
