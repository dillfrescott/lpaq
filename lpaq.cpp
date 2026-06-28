/* lpaq1.cpp file compressor, July 24, 2007.
(C) 2007, Matt Mahoney, matmahoney@yahoo.com

    LICENSE

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details at
    Visit <http://www.gnu.org/copyleft/gpl.html>.
*/

#include <stdio.h>

#if defined(_WIN32) || defined(_WIN64)
  #define _CRT_SECURE_NO_WARNINGS
  #ifndef ftello
    #define ftello _ftelli64
  #endif
  #ifndef fseeko
    #define fseeko _fseeki64
  #endif
#else
  #ifndef _FILE_OFFSET_BITS
    #define _FILE_OFFSET_BITS 64
  #endif
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#define NDEBUG  // remove for debugging
#include <assert.h>


// 8, 32 bit unsigned types
typedef unsigned char  U8;
typedef unsigned int   U32;
typedef uint64_t U64;

// Error handler: print message if any, and exit
void quit(const char* message=0) {
  if (message) printf("%s\n", message);
  exit(1);
}

// Create an array p of n elements of type T
template <class T> void alloc(T*&p, size_t n) {
  p=(T*)calloc(n, sizeof(T));
  if (!p) quit("out of memory");
}

///////////////////////////// Squash //////////////////////////////

// return p = 1/(1 + exp(-d)), d scaled by 8 bits, p scaled by 12 bits
int squash(int d) {
  static const int t[33]={
    1,2,3,6,10,16,27,45,73,120,194,310,488,747,1101,
    1546,2047,2549,2994,3348,3607,3785,3901,3975,4022,
    4050,4068,4079,4085,4089,4092,4093,4094};
  if (d>2047) return 4095;
  if (d<-2047) return 0;
  int w=d&127;
  d=(d>>7)+16;
  return (t[d]*(128-w)+t[(d+1)]*w+64) >> 7;
}

//////////////////////////// Stretch ///////////////////////////////

// Inverse of squash. stretch(d) returns ln(p/(1-p)), d scaled by 8 bits,
// p by 12 bits.  d has range -2047 to 2047 representing -8 to 8.  
// p has range 0 to 4095 representing 0 to 1.

class Stretch {
  short t[4096];
public:
  Stretch();
  int operator()(int p) const {
    assert(p>=0 && p<4096);
    return t[p];
  }
} stretch;

Stretch::Stretch() {
  int pi=0;
  for (int x=-2047; x<=2047; ++x) {  // invert squash()
    int i=squash(x);
    for (int j=pi; j<=i; ++j)
      t[j]=x;
    pi=i+1;
  }
  t[4095]=2047;
}

///////////////////////// state table ////////////////////////

// State table:
//   nex(state, 0) = next state if bit y is 0, 0 <= state < 256
//   nex(state, 1) = next state if bit y is 1
//
// States represent a bit history within some context.
// State 0 is the starting state (no bits seen).
// States 1-30 represent all possible sequences of 1-4 bits.
// States 31-252 represent a pair of counts, (n0,n1), the number
//   of 0 and 1 bits respectively.  If n0+n1 < 16 then there are
//   two states for each pair, depending on if a 0 or 1 was the last
//   bit seen.
// If n0 and n1 are too large, then there is no state to represent this
// pair, so another state with about the same ratio of n0/n1 is substituted.
// Also, when a bit is observed and the count of the opposite bit is large,
// then part of this count is discarded to favor newer data over old.

static const U8 State_table[256][2]={
{  1,  2},{  3,  5},{  4,  6},{  7, 10},{  8, 12},{  9, 13},{ 11, 14}, // 0
{ 15, 19},{ 16, 23},{ 17, 24},{ 18, 25},{ 20, 27},{ 21, 28},{ 22, 29}, // 7
{ 26, 30},{ 31, 33},{ 32, 35},{ 32, 35},{ 32, 35},{ 32, 35},{ 34, 37}, // 14
{ 34, 37},{ 34, 37},{ 34, 37},{ 34, 37},{ 34, 37},{ 36, 39},{ 36, 39}, // 21
{ 36, 39},{ 36, 39},{ 38, 40},{ 41, 43},{ 42, 45},{ 42, 45},{ 44, 47}, // 28
{ 44, 47},{ 46, 49},{ 46, 49},{ 48, 51},{ 48, 51},{ 50, 52},{ 53, 43}, // 35
{ 54, 57},{ 54, 57},{ 56, 59},{ 56, 59},{ 58, 61},{ 58, 61},{ 60, 63}, // 42
{ 60, 63},{ 62, 65},{ 62, 65},{ 50, 66},{ 67, 55},{ 68, 57},{ 68, 57}, // 49
{ 70, 73},{ 70, 73},{ 72, 75},{ 72, 75},{ 74, 77},{ 74, 77},{ 76, 79}, // 56
{ 76, 79},{ 62, 81},{ 62, 81},{ 64, 82},{ 83, 69},{ 84, 71},{ 84, 71}, // 63
{ 86, 73},{ 86, 73},{ 44, 59},{ 44, 59},{ 58, 61},{ 58, 61},{ 60, 49}, // 70
{ 60, 49},{ 76, 89},{ 76, 89},{ 78, 91},{ 78, 91},{ 80, 92},{ 93, 69}, // 77
{ 94, 87},{ 94, 87},{ 96, 45},{ 96, 45},{ 48, 99},{ 48, 99},{ 88,101}, // 84
{ 88,101},{ 80,102},{103, 69},{104, 87},{104, 87},{106, 57},{106, 57}, // 91
{ 62,109},{ 62,109},{ 88,111},{ 88,111},{ 80,112},{113, 85},{114, 87}, // 98
{114, 87},{116, 57},{116, 57},{ 62,119},{ 62,119},{ 88,121},{ 88,121}, // 105
{ 90,122},{123, 85},{124, 97},{124, 97},{126, 57},{126, 57},{ 62,129}, // 112
{ 62,129},{ 98,131},{ 98,131},{ 90,132},{133, 85},{134, 97},{134, 97}, // 119
{136, 57},{136, 57},{ 62,139},{ 62,139},{ 98,141},{ 98,141},{ 90,142}, // 126
{143, 95},{144, 97},{144, 97},{ 68, 57},{ 68, 57},{ 62, 81},{ 62, 81}, // 133
{ 98,147},{ 98,147},{100,148},{149, 95},{150,107},{150,107},{108,151}, // 140
{108,151},{100,152},{153, 95},{154,107},{108,155},{100,156},{157, 95}, // 147
{158,107},{108,159},{100,160},{161,105},{162,107},{108,163},{110,164}, // 154
{165,105},{166,117},{118,167},{110,168},{169,105},{170,117},{118,171}, // 161
{110,172},{173,105},{174,117},{118,175},{110,176},{177,105},{178,117}, // 168
{118,179},{110,180},{181,115},{182,117},{118,183},{120,184},{185,115}, // 175
{186,127},{128,187},{120,188},{189,115},{190,127},{128,191},{120,192}, // 182
{193,115},{194,127},{128,195},{120,196},{197,115},{198,127},{128,199}, // 189
{120,200},{201,115},{202,127},{128,203},{120,204},{205,115},{206,127}, // 196
{128,207},{120,208},{209,125},{210,127},{128,211},{130,212},{213,125}, // 203
{214,137},{138,215},{130,216},{217,125},{218,137},{138,219},{130,220}, // 210
{221,125},{222,137},{138,223},{130,224},{225,125},{226,137},{138,227}, // 217
{130,228},{229,125},{230,137},{138,231},{130,232},{233,125},{234,137}, // 224
{138,235},{130,236},{237,125},{238,137},{138,239},{130,240},{241,125}, // 231
{242,137},{138,243},{130,244},{245,135},{246,137},{138,247},{140,248}, // 238
{249,135},{250, 69},{ 80,251},{140,252},{249,135},{250, 69},{ 80,251}, // 245
{140,252},{  0,  0},{  0,  0},{  0,  0}};  // 252
#define nex(state,sel) State_table[state][sel]

//////////////////////////// StateMap, APM //////////////////////////

// A StateMap maps a context to a probability.  Methods:
//
// Statemap sm(n) creates a StateMap with n contexts using 4*n bytes memory.
// sm.p(y, cx, limit) converts state cx (0..n-1) to a probability (0..4095).
//     that the next y=1, updating the previous prediction with y (0..1).
//     limit (1..1023, default 1023) is the maximum count for computing a
//     prediction.  Larger values are better for stationary sources.

class StateMap {
protected:
  const int N;  // Number of contexts
  int cxt;      // Context of last prediction
  U32 *t;       // cxt -> prediction in high 22 bits, count in low 10 bits
  static int dt[1024];  // i -> 16K/(i+3)
  void update(int y, int limit) {
    assert(cxt>=0 && cxt<N);
    int n=t[cxt]&1023, p=t[cxt]>>10;  // count, prediction
    if (n<limit) ++t[cxt];
    else t[cxt]=t[cxt]&0xfffffc00|limit;
    t[cxt]+=(((y<<22)-p)>>3)*dt[n]&0xfffffc00;
  }
public:
  StateMap(int n=256);

  // update bit y (0..1), predict next bit in context cx
  int p(int y, int cx, int limit=1023) {
    assert(y>>1==0);
    assert(cx>=0 && cx<N);
    assert(limit>0 && limit<1024);
    update(y, limit);
    return t[cxt=cx]>>20;
  }
};

int StateMap::dt[1024]={0};

StateMap::StateMap(int n): N(n), cxt(0) {
  alloc(t, N);
  for (int i=0; i<N; ++i)
    t[i]=1<<31;
  if (dt[0]==0)
    for (int i=0; i<1024; ++i)
      dt[i]=16384/(i+i+3);
}

// An APM maps a probability and a context to a new probability.  Methods:
//
// APM a(n) creates with n contexts using 96*n bytes memory.
// a.pp(y, pr, cx, limit) updates and returns a new probability (0..4095)
//     like with StateMap.  pr (0..4095) is considered part of the context.
//     The output is computed by interpolating pr into 24 ranges nonlinearly
//     with smaller ranges near the ends.  The initial output is pr.
//     y=(0..1) is the last bit.  cx=(0..n-1) is the other context.
//     limit=(0..1023) defaults to 255.

class APM: public StateMap {
public:
  APM(int n);
  int pp(int y, int pr, int cx, int limit=255) {
    assert(y>>1==0);
    assert(pr>=0 && pr<4096);
    assert(cx>=0 && cx<N/24);
    assert(limit>0 && limit<1024);
    update(y, limit);
    pr=(stretch(pr)+2048)*23;
    int wt=pr&0xfff;  // interpolation weight of next element
    cx=cx*24+(pr>>12);
    assert(cx>=0 && cx<N-1);
    pr=(t[cx]>>13)*(0x1000-wt)+(t[cx+1]>>13)*wt>>19;
    cxt=cx+(wt>>11);
    return pr;
  }
};

APM::APM(int n): StateMap(n*24) {
  for (int i=0; i<N; ++i) {
    int p=((i%24*2+1)*4096)/48-2048;
    t[i]=(U32(squash(p))<<20)+6;
  }
}

//////////////////////////// Mixer /////////////////////////////

// Mixer m(N, M) combines models using M neural networks with
//     N inputs each using 4*M*N bytes of memory.  It is used as follows:
// m.update(y) trains the network where the expected output is the
//     last bit, y.
// m.add(stretch(p)) inputs prediction from one of N models.  The
//     prediction should be positive to predict a 1 bit, negative for 0,
//     nominally -2K to 2K.
// m.set(cxt) selects cxt (0..M-1) as one of M neural networks to use.
// m.p() returns the output prediction that the next bit is 1 as a
//     12 bit number (0 to 4095).  The normal sequence per prediction is:
//
// - m.add(x) called N times with input x=(-2047..2047)
// - m.set(cxt) called once with cxt=(0..M-1)
// - m.p() called once to predict the next bit, returns 0..4095
// - m.update(y) called once for actual bit y=(0..1).

class Mixer {
  const int M;      // max contexts
  const int N;      // number of inputs
  int tx[32];       // inputs buffer
  int* wx;          // N*M weights
  int cxt;          // context
  int nx;           // Number of inputs in tx
  int pr;           // last result (scaled 12 bits)
public:
  Mixer(int m, int n = 7);

  // Adjust weights to minimize coding cost of last prediction
  inline void update(int y) {
    int err=((y<<12)-pr)*9;
    assert(err>=-32768 && err<32768);
    int *w = &wx[cxt*N];
    for (int i = 0; i < N; ++i) {
      w[i] += (tx[i]*err + 0x8000) >> 16;
    }
    nx=0;
  }

  // Input x
  inline void add(int x) {
    assert(nx < N);
    tx[nx++]=x;
  }

  // Set a context
  inline void set(int cx) {
    cxt=cx;
  }

  // predict next bit
  inline int p() {
    int sum = 0;
    const int *w = &wx[cxt*N];
    for (int i = 0; i < N; ++i) {
      sum += tx[i] * w[i];
    }
    return pr=squash(sum >> 16);
  }
};

Mixer::Mixer(int m, int n):
    M(m), N(n), wx(0), cxt(0), nx(0), pr(2048) {
  alloc(wx, N*M);
}

//////////////////////////// HashTable /////////////////////////

template <int B>
class HashTable {
  U8* t;
  const size_t N;
public:
  HashTable(size_t n);
  inline U8* operator[](U32 i);
};

template <int B>
HashTable<B>::HashTable(size_t n): t(0), N(n) {
  assert(B>=2 && (B&B-1)==0);
  assert(N>=B*4 && (N&N-1)==0);
  alloc(t, N+B*4+64);
  t+=64-int(((uintptr_t)t)&63);
}

template <int B>
inline U8* HashTable<B>::operator[](U32 i) {
  i*=123456791;
  i=i<<16|i>>16;
  i*=234567891;
  int chk=i>>24;
  i=(i*B)&(N-B);
  if (t[i]==chk) return t+i;
  U32 i1=i^B;
  if (t[i1]==chk) return t+i1;
  U32 i2=i^(B*2);
  if (t[i2]==chk) return t+i2;
  if (t[i+1]>t[i1+1] || t[i+1]>t[i2+1]) i=i1;
  if (t[i+1]>t[(i^B^(B*2))+1]) i^=B^(B*2);
  memset(t+i, 0, B);
  t[i]=chk;
  return t+i;
}

//////////////////////////// MatchModel ////////////////////////

// MatchModel(n) predicts next bit using most recent context match.
//     using n bytes of memory.  n must be a power of 2 at least 8.
// MatchModel::p(y, m) updates the model with bit y (0..1) and writes
//     a prediction of the next bit to Mixer m.  It returns the length of
//     context matched (0..62).

class MatchModel {
  const size_t N;   // last buffer index
  const size_t HN;  // last hash table index
  enum {MAXLEN=62};   // maximum match length, at most 62
  U8* buf;    // input buffer
  size_t* ht; // context hash -> next byte in buf
  size_t pos; // number of bytes in buf
  size_t match; // pointer to current byte in matched context in buf
  int len;    // length of match
  U32 h1, h2; // context hashes
  int c0;     // last 0-7 bits of y
  int bcount; // number of bits in c0 (0..7)
  StateMap sm;  // len, bit, last byte -> prediction
public:
  MatchModel(size_t n);
  int p(int y, Mixer& m);  // update bit y (0..1), predict next bit to m
};

MatchModel::MatchModel(size_t n): N(n/2-1), HN(n/8-1), buf(0), ht(0), pos(0), 
    match(0), len(0), h1(0), h2(0), c0(1), bcount(0), sm(56<<8) {
  assert(n>=8 && (n&(n-1))==0);
  alloc(buf, N+1);
  alloc(ht, HN+1);
}

int MatchModel::p(int y, Mixer& m) {

  // update context
  c0+=c0+y;
  ++bcount;
  if (bcount==8) {
    bcount=0;
    h1=h1*(3<<3)+c0&HN;
    h2=h2*(5<<5)+c0&HN;
    buf[pos++]=c0;
    c0=1;
    pos&=N;

    // find or extend match
    if (len>0) {
      ++match;
      match&=N;
      if (len<MAXLEN) ++len;
    }
    else {
      match=ht[h1];
      if (match!=pos) {
        size_t i;
        while (len<MAXLEN && (i=(match-len-1)&N)!=pos
               && buf[i]==buf[(pos-len-1)&N])
          ++len;
      }
    }
    if (len<2) {
      len=0;
      match=ht[h2];
      if (match!=pos) {
        size_t i;
        while (len<MAXLEN && (i=(match-len-1)&N)!=pos
               && buf[i]==buf[(pos-len-1)&N])
          ++len;
      }
    }
  }

  // predict
  int cxt=c0;
  if (len>0 && (buf[match]+256>>8-bcount)==c0) {
    int b=buf[match]>>7-bcount&1;  // next bit
    if (len<16) cxt=len*2+b;
    else cxt=(len>>2)*2+b+24;
    cxt=cxt*256+buf[pos-1&N];
  }
  else
    len=0;
  m.add(stretch(sm.p(y, cxt)));

  // update index
  if (bcount==0) {
    ht[h1]=pos;
    ht[h2]=pos;
  }
  return len;
}

//////////////////////////// Predictor /////////////////////////

// A Predictor estimates the probability that the next bit of
// uncompressed data is 1.  Methods:
// Predictor(n) creates with 3*n bytes of memory.
// p() returns P(1) as a 12 bit number (0-4095).
// update(y) trains the predictor with the actual bit (0 or 1).

const size_t MEM = 1ULL << (9 + 20);

class Predictor {
  int pr;  // next prediction
public:
  Predictor();
  int p() const {assert(pr>=0 && pr<4096); return pr;}
  void update(int y);
};

Predictor::Predictor(): pr(2048) {}

void Predictor::update(int y) {
  static U8 t0[0x10000];  // order 1 cxt -> state
  static HashTable<16> t(MEM*2ULL);  // cxt -> state
  static int c0=1;  // last 0-7 bits with leading 1
  static int c4=0;  // last 4 bytes
  static U8 *cp[12]={t0, t0, t0, t0, t0, t0, t0, t0, t0, t0, t0, t0};  // pointer to bit history
  static int bcount=0;  // bit count
  static StateMap sm[12];
  static APM a1(0x100), a2(0x4000);
  static U32 h[12];
  static Mixer m(655360, 13);  // 13 inputs (1 match model + 12 state maps)
  static MatchModel mm(MEM);  // predicts next bit by matching context
  assert(MEM>0);

  // update model
  assert(y==0 || y==1);
  for (int i = 0; i < 12; ++i)
    *cp[i]=nex(*cp[i], y);
  m.update(y);

  // update context
  ++bcount;
  c0+=c0+y;
  if (c0>=256) {
    c0-=256;
    c4=c4<<8|c0;
    h[0]=c0<<8;  // order 1
    h[1]=(c4&0xffff)<<5|0x57000000;  // order 2
    h[2]=(c4<<8)*3;  // order 3
    h[3]=c4*5;  // order 4
    h[4]=h[4]*(11<<5)+c0*13&0x3fffffff;  // order 6
    int c0_word = c0;
    if (c0_word>=65 && c0_word<=90) c0_word+=32;  // lowercase unigram word order
    if (c0_word>=97 && c0_word<=122) h[5]=(h[5]+c0_word)*(7<<3);
    else h[5]=0;
    h[6]=h[6]*(13<<5)+c0*17&0x3fffffff;  // order 5 rolling context
    h[7]=((c0<<8)|((c4>>8)&0xff))*9;  // skip-1 context
    h[8]=((c0<<8)|((c4>>16)&0xff))*13;  // skip-2 context
    h[9]=((c4&0xffff00)>>8)*11;  // 2-byte stride context
    if ((c0>='a' && c0<='z') || (c0>='A' && c0<='Z') || (c0>='0' && c0<='9'))
      h[10]=(h[10]+(c0&0x1f))*(5<<3);  // general word context
    else
      h[10]=0;
    h[11]=(((c4&0xff)|((c4>>16)&0xff00))*17)&0x3fffffff;  // sparse order 3 context

    for (int i = 1; i < 12; ++i)
      cp[i]=t[h[i]]+1;
    c0=1;
    bcount=0;
  }
  if (bcount==4) {
    for (int i = 1; i < 12; ++i)
      cp[i]=t[h[i]+c0]+1;
  }
  else if (bcount>0) {
    int j=y+1<<(bcount&3)-1;
    for (int i = 1; i < 12; ++i)
      cp[i]+=j;
  }
  cp[0]=t0+h[0]+c0;

    // predict
    int len = mm.p(y, m);
    int order = 0;
    if (len == 0) {
        if (*cp[6]) ++order;
        if (*cp[4]) ++order;
        if (*cp[3]) ++order;
        if (*cp[2]) ++order;
        if (*cp[1]) ++order;
    }
    else order = 5 + (len >= 8) + (len >= 12) + (len >= 16) + (len >= 32);
    for (int i = 0; i < 12; ++i)
        m.add(stretch(sm[i].p(y, *cp[i])));
    m.set(order + 10 * (c0 | ((c4 & 0xff) << 8)));
    pr = m.p();
    pr = pr + 3 * a1.pp(y, pr, c0) >> 2;
    pr = pr + 3 * a2.pp(y, pr, c0 ^ h[0] >> 2) >> 2;
}

//////////////////////////// Encoder ////////////////////////////

// An Encoder does arithmetic encoding.  Methods:
// Encoder(COMPRESS, f) creates encoder for compression to archive f, which
//     must be open past any header for writing in binary mode.
// Encoder(DECOMPRESS, f) creates encoder for decompression from archive f,
//     which must be open past any header for reading in binary mode.
// code(i) in COMPRESS mode compresses bit i (0 or 1) to file f.
// code() in DECOMPRESS mode returns the next decompressed bit from file f.
// compress(c) in COMPRESS mode compresses one byte.
// decompress() in DECOMPRESS mode decompresses and returns one byte.
// flush() should be called exactly once after compression is done and
//     before closing f.  It does nothing in DECOMPRESS mode.

typedef enum {COMPRESS, DECOMPRESS} Mode;
class Encoder {
private:
  Predictor predictor;
  const Mode mode;       // Compress or decompress?
  FILE* archive;         // Compressed data file
  U32 x1, x2;            // Range, initially [0, 1), scaled by 2^32
  U32 x;                 // Decompress mode: last 4 input bytes of archive

  // Compress bit y or return decompressed bit
  int code(int y=0) {
    int p=predictor.p();
    assert(p>=0 && p<4096);
    p+=p<2048;
    U32 xmid=x1 + (x2-x1>>12)*p + ((x2-x1&0xfff)*p>>12);
    assert(xmid>=x1 && xmid<x2);
    if (mode==DECOMPRESS) y=x<=xmid;
    y ? (x2=xmid) : (x1=xmid+1);
    predictor.update(y);
    while (((x1^x2)&0xff000000)==0) {  // pass equal leading bytes of range
      if (mode==COMPRESS) putc(x2>>24, archive);
      x1<<=8;
      x2=(x2<<8)+255;
      if (mode==DECOMPRESS) x=(x<<8)+(getc(archive)&255);  // EOF is OK
    }
    return y;
  }

public:
  Encoder(Mode m, FILE* f);
  void flush();  // call this when compression is finished

  // Compress one byte
  void compress(int c) {
    assert(mode==COMPRESS);
    for (int i=7; i>=0; --i)
      code((c>>i)&1);
  }

  // Decompress and return one byte
  int decompress() {
    int c=0;
    for (int i=0; i<8; ++i)
      c+=c+code();
    return c;
  }
};

Encoder::Encoder(Mode m, FILE* f):
    mode(m), archive(f), x1(0), x2(0xffffffff), x(0) {
  if (mode==DECOMPRESS) {  // x = first 4 bytes of archive
    for (int i=0; i<4; ++i)
      x=(x<<8)+(getc(archive)&255);
  }
}

void Encoder::flush() {
  if (mode==COMPRESS)
    putc(x1>>24, archive);  // Flush first unequal byte of range
}

//////////////////////////// User Interface ////////////////////////////

static bool ends_with_lpaq(const char *str) {
    size_t len = strlen(str);
    if (len < 5) return false;
    const char *ext = str + len - 5;
    return (ext[0] == '.') &&
           (ext[1] == 'l' || ext[1] == 'L') &&
           (ext[2] == 'p' || ext[2] == 'P') &&
           (ext[3] == 'a' || ext[3] == 'A') &&
           (ext[4] == 'q' || ext[4] == 'Q');
}

int main(int argc, char **argv) {
    char mode = 0;
    const char *in_filename = NULL;
    char *out_filename_alloc = NULL;
    const char *out_filename = NULL;

    if (argc == 2) {
        in_filename = argv[1];
        size_t len = strlen(in_filename);
        if (ends_with_lpaq(in_filename)) {
            mode = 'd';
            size_t out_len = len - 5;
            out_filename_alloc = (char *)malloc(out_len + 1);
            if (!out_filename_alloc) quit("out of memory");
            memcpy(out_filename_alloc, in_filename, out_len);
            out_filename_alloc[out_len] = '\0';
            out_filename = out_filename_alloc;
        } else {
            mode = 'c';
            size_t out_len = len + 5;
            out_filename_alloc = (char *)malloc(out_len + 1);
            if (!out_filename_alloc) quit("out of memory");
            snprintf(out_filename_alloc, out_len + 1, "%s.lpaq", in_filename);
            out_filename = out_filename_alloc;
        }
    } else {
        printf("Usage: lpaq <file>  (compresses <file> to <file>.lpaq, or decompresses <file>.lpaq)\n");
        return 1;
    }

    clock_t start = clock();

    FILE *in = fopen(in_filename, "rb");
    if (!in) {
        perror(in_filename);
        if (out_filename_alloc) free(out_filename_alloc);
        return 1;
    }
    FILE *out = NULL;

    // Compress
    if (mode == 'c') {
        fseeko(in, 0, SEEK_END);
        uint64_t size = ftello(in);
        fseeko(in, 0, SEEK_SET);

        if (size == 0) {
            printf("Input file is empty.\n");
            fclose(in);
            if (out_filename_alloc) free(out_filename_alloc);
            return 1;
        }

        out = fopen(out_filename, "wb");
        if (!out) {
            perror(out_filename);
            fclose(in);
            if (out_filename_alloc) free(out_filename_alloc);
            return 1;
        }
        // Write header and file size
        fprintf(out, "pQ%c%c", 2, '9');
        for (int i = 7; i >= 0; --i) {
            putc((size >> (i * 8)) & 0xFF, out);
        }

        Encoder e(COMPRESS, out);
        int c;
        uint64_t processed = 0, last_update = 0;
        const uint64_t update_interval = (size >= 10000) ? (size / 10000) : 1;
        int first_update = 1;

        // Initial progress message
        printf("Compressing: 0.00%%");
        fflush(stdout);

        while ((c = getc(in)) != EOF) {
            e.compress(c);
            processed++;

            if (first_update || processed - last_update >= update_interval || processed == size) {
                double progress = (double)processed / size * 100.0;
                printf("\rCompressing: %.2f%%", progress);
                fflush(stdout);
                last_update = processed;
                first_update = 0;
            }
        }

        e.flush();
        printf("\rCompressing: 100.00%%");
        fflush(stdout);
    }
    // Decompress
    else {  // mode == 'd'
        if (getc(in) != 'p' || getc(in) != 'Q' || getc(in) != 2 || getc(in) != '9') {
            printf("Not a compatible lpaq file (wrong version or compression level).\n");
            fclose(in);
            if (out_filename_alloc) free(out_filename_alloc);
            return 1;
        }

        uint64_t size = 0;
        for (int i = 7; i >= 0; --i) {
            int byte = getc(in);
            if (byte == EOF) {
                printf("Unexpected EOF while reading file size.\n");
                fclose(in);
                if (out_filename_alloc) free(out_filename_alloc);
                return 1;
            }
            size |= (uint64_t)byte << (i * 8);
        }
        if (size == 0) {
            printf("File is empty\n");
            fclose(in);
            if (out_filename_alloc) free(out_filename_alloc);
            return 0;
        }
        uint64_t originalSize = size;
        out = fopen(out_filename, "wb");
        if (!out) {
            perror(out_filename);
            fclose(in);
            if (out_filename_alloc) free(out_filename_alloc);
            return 1;
        }

        Encoder e(DECOMPRESS, in);
        uint64_t processed = 0, last_update = 0;
        const uint64_t update_interval = (size >= 10000) ? (size / 10000) : 1;
        int first_update = 1;

        // Initial progress message
        printf("Decompressing: 0.00%%");
        fflush(stdout);

        while (processed < originalSize) {
            int c = e.decompress();
            if (putc(c, out) == EOF)
                break;
            processed++;

            if (first_update || processed - last_update >= update_interval || processed == originalSize) {
                double progress = (double)processed / originalSize * 100.0;
                printf("\rDecompressing: %.2f%%", progress);
                fflush(stdout);
                last_update = processed;
                first_update = 0;
            }
        }
        printf("\rDecompressing: 100.00%%");
        fflush(stdout);
    }

    assert(in);
    assert(out);
    printf("\n%" PRIu64 " -> %" PRIu64 " in %1.2f sec. using %d MB memory.\n",
           (U64)ftello(in), (U64)ftello(out), double(clock() - start) / CLOCKS_PER_SEC,
           3 + (MEM >> 20) * 3);

    fclose(in);
    if (out) fclose(out);
    if (out_filename_alloc) free(out_filename_alloc);
    return 0;
}
