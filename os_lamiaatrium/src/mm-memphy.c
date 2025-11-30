/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef IODUMP
#define IOLOG(fmt, ...) \
    do { printf("[MEMPHY] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define IOLOG(fmt, ...) do {} while (0)
#endif

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   int numstep = 0;

   mp->cursor = 0;
   while (numstep < offset && numstep < mp->maxsz)
   {
      /* Traverse sequentially */
      mp->cursor = (mp->cursor + 1) % mp->maxsz;
      numstep++;
   }

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg)   /* random device cannot use seq read */
      return -1;

   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE)mp->storage[mp->cursor];

   IOLOG("seq_read: addr=%llu value=%u",
         (unsigned long long)addr, (unsigned)*value);
   return 0;
}

int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg) {
      *value = mp->storage[addr];
      IOLOG("read: rdm addr=%llu value=%u",
            (unsigned long long)addr, (unsigned)*value);
   } else {
      /* sequential access device */
      return MEMPHY_seq_read(mp, addr, value);
   }

   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg)
      return -1;  /* random device cannot use seq write */

   MEMPHY_mv_csr(mp, addr);
   mp->storage[mp->cursor] = value;

   IOLOG("seq_write: addr=%llu value=%u",
         (unsigned long long)addr, (unsigned)value);
   return 0;
}

int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp == NULL)
      return -1;

   if (mp->rdmflg) {
      mp->storage[addr] = data;
      IOLOG("write: rdm addr=%llu value=%u",
            (unsigned long long)addr, (unsigned)data);
   } else {
      return MEMPHY_seq_write(mp, addr, data);
   }

   return 0;
}

/*
 *  MEMPHY_format - format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   int numfp = mp->maxsz / pagesz;
   struct framephy_struct *newfst, *fst;
   int iter = 0;

   if (numfp <= 0)
      return -1;

   fst = malloc(sizeof(struct framephy_struct));
   fst->fpn = iter;
   fst->fp_next = NULL;
   mp->free_fp_list = fst;

   for (iter = 1; iter < numfp; iter++) {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }

   mp->used_fp_list = NULL;

   IOLOG("format: maxsz=%d pagesz=%d numfp=%d", mp->maxsz, pagesz, numfp);
   return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
    if (mp == NULL) {
        IOLOG("get_freefp: mp == NULL (BUG: kernel mram not set?)");
        return -1;
    }

    struct framephy_struct *fp = mp->free_fp_list;

    if (fp == NULL)
        return -1;

    *retfpn = fp->fpn;
    mp->free_fp_list = fp->fp_next;

    IOLOG("get_freefp: fpn=%llu",
          (unsigned long long)*retfpn);

    free(fp);
    return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));
   newnode->fpn = fpn;
   newnode->fp_next = mp->free_fp_list;
   mp->free_fp_list = newnode;

   IOLOG("put_freefp: fpn=%llu", (unsigned long long)fpn);
   return 0;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
#ifdef IODUMP
   if (!mp) {
      printf("[MEMPHY] dump: mp == NULL\n");
      return -1;
   }

   printf("[MEMPHY] dump: maxsz=%d rdmflg=%d\n", mp->maxsz, mp->rdmflg);
   int limit = mp->maxsz < 256 ? mp->maxsz : 256;
   for (int i = 0; i < limit; i++) {
      if (i % 16 == 0)
         printf("\n  %04x: ", i);
      printf("%02x ", mp->storage[i] & 0xff);
   }
   printf("\n");
#endif
   return 0;
}

int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz   = max_size;

   if (!mp->storage)
       return -1;

   memset(mp->storage, 0, max_size * sizeof(BYTE));

   mp->free_fp_list = NULL;
   mp->used_fp_list = NULL;

   MEMPHY_format(mp, PAGING_PAGESZ);

   mp->rdmflg = (randomflg != 0) ? 1 : 0;
   if (!mp->rdmflg)
      mp->cursor = 0;
   else
      mp->cursor = -1;

   IOLOG("init_memphy: max_size=%llu rdmflg=%d",
         (unsigned long long)max_size, mp->rdmflg);

   return 0;
}
