// ./fsrecov-64 ../fsrecov.img
#include "fat32.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct fat32hdr *hdr;

typedef struct {      // Total: 54 bytes
  uint16_t type;      // Magic identifier: 0x4d42
  uint32_t size;      // File size in bytes
  uint16_t reserved1; // Not used
  uint16_t reserved2; // Not used
  uint32_t
      offset; // Offset to image data in bytes from beginning of file (54 bytes)
  uint32_t dib_header_size;  // DIB Header size in bytes (40 bytes)
  int32_t width_px;          // Width of the image
  int32_t height_px;         // Height of image
  uint16_t num_planes;       // Number of color planes
  uint16_t bits_per_pixel;   // Bits per pixel
  uint32_t compression;      // Compression type
  uint32_t image_size_bytes; // Image size in bytes
  int32_t x_resolution_ppm;  // Pixels per meter
  int32_t y_resolution_ppm;  // Pixels per meter
  uint32_t num_colors;       // Number of colors
  uint32_t important_colors; // Important colors
} BMPHeader;

typedef struct {
  BMPHeader header;
  unsigned char *data;
} BMPImage;

enum cluster_type {
  DIR,
  BMPHEADER,
  BMPDATA,
};

typedef struct {
  enum cluster_type type;
} clusterInfo;

typedef struct {
  char shortname[32];
  u16 longname[64];
  u32 dataClus;
  u32 size;
  u8 checksum;
  char sha1[40];
} bmpfile;

void *mmap_disk(const char *fname);
void find_cluster_type(int clusId, clusterInfo *clusters);
void *cluster_address(int n);
int scan_dents_in_cluster(int clusId, clusterInfo *clusters);

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
  int numclusters = hdr->BPB_TotSec32 / hdr->BPB_SecPerClus;
  clusterInfo clus_info[numclusters + 2];
  for (int clusId = hdr->BPB_RootClus; clusId < numclusters; clusId++) {
    find_cluster_type(clusId, clus_info);
  }
  int num_bmp_files = 0;
  for (int clusId = hdr->BPB_RootClus; clusId < numclusters; clusId++) {
    if (clus_info[clusId].type == DIR) {
      num_bmp_files += scan_dents_in_cluster(clusId, clus_info);
    }
  }
  printf("num of bmp files: %d\n", num_bmp_files);
  munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);
}

void get_filename(struct fat32dent *dent, char *buf) {
  // RTFM: Sec 6.1

  int len = 0;
  for (int i = 0; i < sizeof(dent->DIR_Name); i++) {
    if (dent->DIR_Name[i] != ' ') {
      if (i == 8)
        buf[len++] = '.';
      buf[len++] = dent->DIR_Name[i];
    }
  }
  buf[len] = '\0';
}
u32 next_cluster(int n) {
  // RTFM: Sec 4.1

  u32 off = hdr->BPB_RsvdSecCnt * hdr->BPB_BytsPerSec;
  u32 *fat = (u32 *)((u8 *)hdr + off);
  return fat[n];
}
void *cluster_address(int n) {
  // RTFM: Sec 3.5 and 4 (TRICKY)
  // Don't copy code. Write your own.

  u32 DataSec = hdr->BPB_RsvdSecCnt + hdr->BPB_NumFATs * hdr->BPB_FATSz32;
  DataSec += (n - 2) * hdr->BPB_SecPerClus;
  return ((char *)hdr) + DataSec * hdr->BPB_BytsPerSec;
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

  struct fat32hdr *hdr =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (hdr == MAP_FAILED) {
    goto release;
  }

  close(fd);

  assert(hdr->Signature_word == 0xaa55); // this is an MBR
  assert(hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec == size);

  printf("%s: DOS/MBR boot sector \n", fname);
  printf("OEM-ID \"%s\"\n", hdr->BS_OEMName);
  printf("sectors/cluster %d \n", hdr->BPB_SecPerClus);
  printf("sectors %d \n", hdr->BPB_TotSec32);
  printf("sectors/FAT %d \n", hdr->BPB_FATSz32);
  printf("serial number 0x%x\n", hdr->BS_VolID);
  printf("reserved sectors cnt %d\n", hdr->BPB_RsvdSecCnt);
  printf("num fats %d\n", hdr->BPB_NumFATs);
  printf("Sector number of FSINFO structure in the reserved area \
        of the FAT32 volume is %d.\n",
         hdr->BPB_FSInfo);
  return hdr;

release:
  perror("map disk");
  if (fd > 0) {
    close(fd);
  }
  exit(1);
}

int is_dir_type(struct fat32dent *dent) {
  //扫描cluster之后的该cluster的所有字符，若出现多次BMP字符，则为DIRtype
  int count = 0;
  int cluster_bytes = hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus;
  struct fat32dent *end = dent + cluster_bytes / 32;
  //从第8个字符开始检查是否是"bmp"
  for (struct fat32dent *p = dent; p < end; p++) {
    if (memcmp(p->DIR_Name + 8, "BMP", 3) == 0) {
      count++;
    }
    if (count > 3) {
      return 1;
    }
  }
  return 0;
}
int is_bmp_header_type(struct fat32dent *dent) {
  char *p = (char *)dent;
  if (memcmp(p, "BM", 2) == 0) {
    return 1;
  }
  return 0;
}
void find_cluster_type(int clusId, clusterInfo *clusters) {
  struct fat32dent *dent = (struct fat32dent *)cluster_address(clusId);
  if (is_dir_type(dent)) {
    clusters[clusId].type = DIR;
  } else if (is_bmp_header_type(dent)) {
    clusters[clusId].type = BMPHEADER;
  } else {
    clusters[clusId].type = BMPDATA;
  }
}

void write_to_temp_file(char temp_path[], void *data, bmpfile bmpf) {
  int fd = mkstemp(temp_path); // 创建临时文件
  if (fd < 0) {
    perror("mkstemp");
    exit(EXIT_FAILURE);
  }

  // 将数据写入文件
  int size = bmpf.size;
  ssize_t written = write(fd, data, size);
  if (written < size) {
    perror("write");
    close(fd);
    exit(EXIT_FAILURE);
  }

  close(fd); // 关闭文件
  printf("Data written to temporary file: %s\n", temp_path);
}
void calc_sha1(bmpfile *bmpf) {
  void *data = cluster_address(bmpf->dataClus);
  int size = bmpf->size;
  // 将数据写入临时文件
  char temp_path[] = "/tmp/tempfileXXXXXX";
  write_to_temp_file(temp_path, data, *bmpf);
  char command[256];
  sprintf(command, "sha1sum %s", temp_path);
  printf("Data written to temporary file: %s\n", temp_path);
  FILE *fp = popen(command, "r");
  // 替换 panic_on(fp < 0, "popen"); 为以下代码：
  if (fp < 0) {
    perror("popen");
    exit(EXIT_FAILURE);
  }
  fscanf(fp, "%s", bmpf->sha1); // Get it!
  pclose(fp);
}
void get_longname(struct fat32dent *dent, bmpfile *bmpf) {
  struct fat32dent *low = cluster_address(hdr->BPB_RootClus);
  int longname_idx = 0;
  for (int i = 1; low + i < dent; i++) {
    struct fat32LongNamedent *longName = (struct fat32LongNamedent *)(dent - i);
    if (longName->LDIR_Attr == ATTR_LONG_NAME) {
      assert(longName->LDIR_Ord == i ||
             longName->LDIR_Ord == (LAST_LONG_ENTRY | i));
      if (i == 1) {
        //记录checksum之后验证
        bmpf->checksum = longName->LDIR_Chksum;
      }
      for (int j = 0; j < 5; j++) {
        u16 c = longName->LDIR_Name1[j];
        if (c == 0xFFFF)
          break; // 结束符
        bmpf->longname[longname_idx++] = c;
      }

      // 第2部分（6字符）
      for (int j = 0; j < 6; j++) {
        u16 c = longName->LDIR_Name2[j];
        if (c == 0xFFFF)
          break;
        bmpf->longname[longname_idx++] = c;
      }

      // 第3部分（2字符）
      for (int j = 0; j < 2; j++) {
        u16 c = longName->LDIR_Name3[j];
        if (c == 0xFFFF)
          break;
        bmpf->longname[longname_idx++] = c;
      }

      if (longName->LDIR_Ord & LAST_LONG_ENTRY) {
        break;
      }
    }
  }
}

int scan_dents_in_cluster(int clusId, clusterInfo *clusters) {
  struct fat32dent *dent = (struct fat32dent *)cluster_address(clusId);
  struct fat32dent *end = (struct fat32dent *)cluster_address(clusId + 1);
  bmpfile bmpf;
  int count = 0;
  while (dent < end) {
    if (memcmp(dent->DIR_Name + 8, "BMP", 3) == 0) {
      get_filename(dent, bmpf.shortname);
      bmpf.size = dent->DIR_FileSize;
      bmpf.dataClus = dent->DIR_FstClusLO | (dent->DIR_FstClusHI << 16);
      //目录项确实是一个bmp文件
      if (clusters[bmpf.dataClus].type == BMPHEADER) {
        count++;
        printf("dent short name[%-12s] %6.1lf KiB    ", dent->DIR_Name,
               dent->DIR_FileSize / 1024.0);
        printf("dent bmp file data clus :%d\n", bmpf.dataClus);
        calc_sha1(&bmpf);
        printf("bmp file sha1: %s\n", bmpf.sha1);
        get_longname(dent, &bmpf);
      }
    }
    dent++;
  }
  return count;
}