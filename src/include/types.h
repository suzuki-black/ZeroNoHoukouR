/* types.h — 全モジュール共通の整数型。SDCC z80。 */
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char  u8;
typedef signed   char  s8;
typedef unsigned int   u16;
typedef signed   int   s16;
typedef unsigned long  u32;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* TYPES_H */
