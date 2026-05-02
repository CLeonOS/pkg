#include "pkg_internal.h"

unsigned int pkg_sha256_rotr(unsigned int value, unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

unsigned int pkg_sha256_load_be32(const pkg_u8 *data) {
    return ((unsigned int)data[0] << 24U) | ((unsigned int)data[1] << 16U) | ((unsigned int)data[2] << 8U) |
           (unsigned int)data[3];
}

void pkg_sha256_store_be32(unsigned int value, pkg_u8 *out) {
    out[0] = (pkg_u8)((value >> 24U) & 0xFFU);
    out[1] = (pkg_u8)((value >> 16U) & 0xFFU);
    out[2] = (pkg_u8)((value >> 8U) & 0xFFU);
    out[3] = (pkg_u8)(value & 0xFFU);
}

void pkg_sha256_transform(pkg_sha256_ctx *ctx, const pkg_u8 data[64]) {
    static const unsigned int k[64] = {
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
        0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
        0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
        0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
        0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
        0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
        0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
    };
    unsigned int m[64];
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int d;
    unsigned int e;
    unsigned int f;
    unsigned int g;
    unsigned int h;
    unsigned int i;

    for (i = 0U; i < 16U; i++) {
        m[i] = pkg_sha256_load_be32(data + (i * 4U));
    }
    for (i = 16U; i < 64U; i++) {
        unsigned int s0 = pkg_sha256_rotr(m[i - 15U], 7U) ^ pkg_sha256_rotr(m[i - 15U], 18U) ^ (m[i - 15U] >> 3U);
        unsigned int s1 = pkg_sha256_rotr(m[i - 2U], 17U) ^ pkg_sha256_rotr(m[i - 2U], 19U) ^ (m[i - 2U] >> 10U);
        m[i] = m[i - 16U] + s0 + m[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0U; i < 64U; i++) {
        unsigned int s1 = pkg_sha256_rotr(e, 6U) ^ pkg_sha256_rotr(e, 11U) ^ pkg_sha256_rotr(e, 25U);
        unsigned int ch = (e & f) ^ ((~e) & g);
        unsigned int temp1 = h + s1 + ch + k[i] + m[i];
        unsigned int s0 = pkg_sha256_rotr(a, 2U) ^ pkg_sha256_rotr(a, 13U) ^ pkg_sha256_rotr(a, 22U);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void pkg_sha256_init(pkg_sha256_ctx *ctx) {
    ctx->datalen = 0U;
    ctx->bitlen = 0ULL;
    ctx->state[0] = 0x6A09E667U;
    ctx->state[1] = 0xBB67AE85U;
    ctx->state[2] = 0x3C6EF372U;
    ctx->state[3] = 0xA54FF53AU;
    ctx->state[4] = 0x510E527FU;
    ctx->state[5] = 0x9B05688CU;
    ctx->state[6] = 0x1F83D9ABU;
    ctx->state[7] = 0x5BE0CD19U;
}

void pkg_sha256_update(pkg_sha256_ctx *ctx, const pkg_u8 *data, u64 len) {
    u64 i;

    for (i = 0ULL; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64U) {
            pkg_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512ULL;
            ctx->datalen = 0U;
        }
    }
}

void pkg_sha256_final(pkg_sha256_ctx *ctx, pkg_u8 hash[32]) {
    unsigned int i = ctx->datalen;
    u64 bitlen;

    if (ctx->datalen < 56U) {
        ctx->data[i++] = 0x80U;
        while (i < 56U) {
            ctx->data[i++] = 0U;
        }
    } else {
        ctx->data[i++] = 0x80U;
        while (i < 64U) {
            ctx->data[i++] = 0U;
        }
        pkg_sha256_transform(ctx, ctx->data);
        for (i = 0U; i < 56U; i++) {
            ctx->data[i] = 0U;
        }
    }

    bitlen = ctx->bitlen + ((u64)ctx->datalen * 8ULL);
    ctx->data[56] = (pkg_u8)((bitlen >> 56U) & 0xFFU);
    ctx->data[57] = (pkg_u8)((bitlen >> 48U) & 0xFFU);
    ctx->data[58] = (pkg_u8)((bitlen >> 40U) & 0xFFU);
    ctx->data[59] = (pkg_u8)((bitlen >> 32U) & 0xFFU);
    ctx->data[60] = (pkg_u8)((bitlen >> 24U) & 0xFFU);
    ctx->data[61] = (pkg_u8)((bitlen >> 16U) & 0xFFU);
    ctx->data[62] = (pkg_u8)((bitlen >> 8U) & 0xFFU);
    ctx->data[63] = (pkg_u8)(bitlen & 0xFFU);
    pkg_sha256_transform(ctx, ctx->data);

    for (i = 0U; i < 8U; i++) {
        pkg_sha256_store_be32(ctx->state[i], hash + (i * 4U));
    }
}

int pkg_sha256_file_hex(const char *path, char out_hex[PKG_SHA256_MAX]) {
    pkg_sha256_ctx ctx;
    pkg_u8 hash[32];
    u64 fd;
    u64 i;

    if (path == (const char *)0 || out_hex == (char *)0) {
        return 0;
    }
    out_hex[0] = '\0';

    fd = cleonos_sys_fd_open(path, CLEONOS_O_RDONLY, 0ULL);
    if (fd == (u64)-1) {
        return 0;
    }

    pkg_sha256_init(&ctx);
    for (;;) {
        u64 got = cleonos_sys_fd_read(fd, pkg_copy_buf, (u64)sizeof(pkg_copy_buf));
        if (got == (u64)-1) {
            (void)cleonos_sys_fd_close(fd);
            return 0;
        }
        if (got == 0ULL) {
            break;
        }
        pkg_sha256_update(&ctx, pkg_copy_buf, got);
    }
    (void)cleonos_sys_fd_close(fd);

    pkg_sha256_final(&ctx, hash);
    for (i = 0ULL; i < 32ULL; i++) {
        out_hex[i * 2ULL] = pkg_hex_digit(((u64)hash[i]) >> 4U);
        out_hex[i * 2ULL + 1ULL] = pkg_hex_digit((u64)hash[i]);
    }
    out_hex[64] = '\0';
    return 1;
}

int pkg_file_has_elf_magic(const char *path) {
    u64 fd;
    pkg_u8 magic[4];
    u64 got;

    fd = cleonos_sys_fd_open(path, CLEONOS_O_RDONLY, 0ULL);
    if (fd == (u64)-1) {
        return 0;
    }

    got = cleonos_sys_fd_read(fd, magic, (u64)sizeof(magic));
    (void)cleonos_sys_fd_close(fd);

    return (got == 4ULL && magic[0] == 0x7FU && magic[1] == (pkg_u8)'E' && magic[2] == (pkg_u8)'L' &&
            magic[3] == (pkg_u8)'F')
               ? 1
               : 0;
}
