#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "fat32.h"

struct fat32hdr *hdr;

void *mmap_disk(const char *fname);
void dfs_scan(u32 clusId, int depth, int is_dir);

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s fs-image\n", argv[0]);
        exit(1);
    }

    setbuf(stdout, NULL);

    assert(sizeof(struct fat32hdr) == 512);
    assert(sizeof(struct fat32dent) == 32);

    // Map disk image to memory.
    // The file system is a in-memory data structure now.
    hdr = mmap_disk(argv[1]);

    // File system traversal.
    // dfs_scan(hdr->BPB_RootClus, 0, 1);

    munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);
}

void *mmap_disk(const char *fname) {
    int fd = open(fname, O_RDWR);

    if (fd < 0) {
        goto release;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        goto release;
    }

    struct fat32hdr *hdr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (hdr == MAP_FAILED) {
        goto release;
    }

    close(fd);

    assert(hdr->Signature_word == 0xaa55); // this is an MBR
    assert(hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec == size);

    printf("%s: DOS/MBR boot sector \n", fname);
    printf("OEM-ID \"%s\"\n", hdr->BS_OEMName);
    printf("sectors/cluster %d \n ", hdr->BPB_SecPerClus);
    printf("sectors %d \n", hdr->BPB_TotSec32);
    printf("sectors/FAT %d \n", hdr->BPB_FATSz32);
    printf("serial number 0x%x\n", hdr->BS_VolID);
    printf("reserved sectors cnt %d\n",hdr->BPB_RsvdSecCnt);
    printf("num fats %d\n",hdr->BPB_NumFATs);
    printf("Sector number of FSINFO structure in the reserved area \
        of the FAT32 volume is %d.\n",hdr->BPB_FSInfo);
    return hdr;

release:
    perror("map disk");
    if (fd > 0) {
        close(fd);
    }
    exit(1);
}