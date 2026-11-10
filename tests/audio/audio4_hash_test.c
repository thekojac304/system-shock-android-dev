#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "Audio4Hash.h"

int main(void) {
    const char *path = "audio4-hash-fixture.bin";
    FILE *f = fopen(path, "wb");
    assert(f && fwrite("abc", 1, 3, f) == 3 && fclose(f) == 0);
    char hex[65];
    assert(audio4_sha256_file(path, hex));
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    assert(remove(path) == 0);
    puts("AUDIO4 SHA-256 fixture: PASS");
    return 0;
}
