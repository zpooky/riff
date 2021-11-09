#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* https://sites.google.com/site/musicgapi/technical-documents/wav-file-format
 * http://www.robotplanet.dk/audio/wav_meta_data/
 * http://soundfile.sapp.org/doc/WaveFormat/
 */

typedef unsigned char u8;

static const char *
AudioFormat(uint16_t format);

static void
print_raw(const char *it, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (it[i] == '\0') {
      printf("\\0");
    } else if (it[i] == '\n') {
      printf("\\n");
    } else if (it[i] >= ' ' && it[i] <= '~') {
      printf("%c", it[i]);
    } else {
      printf("\\??");
    }
  }
}

static int
is_ascii(const char *buf, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (!isascii(buf[i])) {
      return 0;
    }
  }

  return 1;
}

static size_t
remaining_read(const u8 *it, const u8 *end) {
  uintptr_t f = (uintptr_t)it;
  uintptr_t s = (uintptr_t)end;
  assert(f <= s);
  return s - f;
}

static const u8 *
read_bytes(const u8 *it, const u8 *end, void *buf, size_t bytes) {
  if (remaining_read(it, end) < bytes) {
    return NULL;
  }
  memcpy(buf, it, bytes);

  return it + bytes;
}
/*
 * Track Artist (IART)
 * Album Artist (IAAR)
 * Composer (ICOM/IMUS)
 * Title (INAM)
 * Product (IPRD) - "Album Title"
 * Album Title (IALB)
 * Track Number (ITRK)
 * Date Created (ICRD/IYER) - "year"
 * Genre (IGNR/IGRE)
 * Comments (ICMT)
 * Copyright (ICOP)
 * Software (ISFT)
 */

static int
parse_subchunk_INFO(const u8 *raw, size_t length) {
  char buf[4];
  const u8 *it = raw;
  const u8 *const end = raw + length;

  if (!(it = read_bytes(it, end, buf, sizeof(buf)))) {
    return EXIT_FAILURE;
  }

  if (memcmp(buf, "INFO", sizeof(buf)) == 0) {
    printf("%.*s[\n", (int)sizeof(buf), buf);
    while (remaining_read(it, end)) {
      uint32_t size;
      int extra = 0;
      if (!(it = read_bytes(it, end, buf, sizeof(buf)))) {
        return EXIT_FAILURE;
      }
      printf("\t%.*s[", (int)sizeof(buf), buf);
      if (!(it = read_bytes(it, end, &size, sizeof(size)))) {
        return EXIT_FAILURE;
      }
      printf("size: %u, '", size);
      if ((size_t)size > remaining_read(it, end)) {
        fprintf(stderr, "ERROR: INFO subcunk size[%u] exceeds size[%zu]\n",
                size, remaining_read(it, end));
        return EXIT_FAILURE;
      }
      print_raw((const char *)it, size);

      printf("']");
      it += size;
      while (remaining_read(it, end) > 0 && *it == '\0') {
        if (!extra) {
          printf("Extra[");
        }
        printf("\\0");
        extra = 1;
        ++it;
      }
      if (extra) {
        printf("]");
      }
      printf("\n");
    } // while
    printf("]");
  } else {
    printf("...");
  }

  return EXIT_SUCCESS;
}

static int
parse_RIFF(const u8 *raw, size_t length) {
  char buf[4];
  const u8 *it = raw;
  const u8 *const end = raw + length;
  int subchunk = 0;

  {
    uint32_t chunk_size;

    if (!(it = read_bytes(it, end, buf, sizeof(buf)))) {
      return EXIT_FAILURE;
    }

    /* TODO RIFF is LE and RIFX is BE */
    if (memcmp(buf, "RIFF", sizeof(buf)) != 0) {
      return EXIT_FAILURE;
    }
    printf("%.*s[", (int)sizeof(buf), buf);

    if (!(it = read_bytes(it, end, &chunk_size, sizeof(chunk_size)))) {
      return EXIT_FAILURE;
    }
    /* chunk_size = ntohl(chunk_size); */
    printf("ChunkSize: %u, ", chunk_size);
    if ((size_t)chunk_size > remaining_read(it, end)) {
      fprintf(stderr,
              "ERROR: RIFF header ChunkSize[%u] is larger then the remaining "
              "file size[%zu]\n",
              chunk_size, remaining_read(it, end));
      return EXIT_FAILURE;
    }

    if (!(it = read_bytes(it, end, buf, sizeof(buf)))) {
      return EXIT_FAILURE;
    }
    printf("Format: '%.*s']\n", (int)sizeof(buf), buf);
  }

  {
    uint32_t bytes4;
    uint16_t bytes2;

    if (!(it = read_bytes(it, end, buf, sizeof(buf)))) {
      return EXIT_FAILURE;
    }
    if (memcmp(buf, "fmt ", sizeof(buf)) != 0) {
      return EXIT_FAILURE;
    }
    printf("[SubChunk%dId: '%.*s', ", ++subchunk, 4, buf);

    if (!(it = read_bytes(it, end, &bytes4, sizeof(bytes4)))) {
      return EXIT_FAILURE;
    }
    /* bytes4 = ntohl(bytes4); */
    printf("size: %u, ", bytes4);

    if (!(it = read_bytes(it, end, &bytes2, sizeof(bytes2)))) {
      return EXIT_FAILURE;
    }
    /* bytes2 = ntohs(bytes2); */
    printf("AudioFormat: '%s', ", AudioFormat(bytes2));

    if (!(it = read_bytes(it, end, &bytes2, sizeof(bytes2)))) {
      return EXIT_FAILURE;
    }
    /* bytes2 = ntohs(bytes2); */
    printf("NumChannels: %u, ", bytes2);

    if (!(it = read_bytes(it, end, &bytes4, sizeof(bytes4)))) {
      return EXIT_FAILURE;
    }
    /* bytes4 = ntohl(bytes4); */
    printf("SampleRate: %u, ", bytes4);

    if (!(it = read_bytes(it, end, &bytes4, sizeof(bytes4)))) {
      return EXIT_FAILURE;
    }
    /* bytes4 = ntohl(bytes4); */
    printf("ByteRate: %u, ", bytes4);

    if (!(it = read_bytes(it, end, &bytes2, sizeof(bytes2)))) {
      return EXIT_FAILURE;
    }
    /* bytes2 = ntohs(bytes2); */
    printf("BlockAlign: %u, ", bytes2);

    if (!(it = read_bytes(it, end, &bytes2, sizeof(bytes2)))) {
      return EXIT_FAILURE;
    }
    /* bytes2 = ntohs(bytes2); */
    printf("BitsPerSample: %u]\n", bytes2);
  }

  while (remaining_read(it, end) > 0) {
    uint32_t size;
    if (!(it = read_bytes(it, end, buf, sizeof(buf)))) {
      return EXIT_FAILURE;
    }
    if (!is_ascii(buf, sizeof(buf))) {
      printf("'%.*s'\n", (int)sizeof(buf), buf);
      return EXIT_FAILURE;
    }
    if (!(it = read_bytes(it, end, &size, sizeof(size)))) {
      return EXIT_FAILURE;
    }
    if (size > remaining_read(it, end)) {
      fprintf(
          stderr,
          "ERROR: SubChunk%dId size[%u] extends above the remaining size of "
          "file[%zu]\n",
          subchunk, size, remaining_read(it, end));
      return EXIT_FAILURE;
    }
    printf("[SubChunk%dId: '%.*s', ", ++subchunk, 4, buf);
    /* size = ntohl(size); */
    printf("size: %u, ", size);
    if (memcmp("LIST", buf, sizeof(buf)) == 0) {
      int res;
      if ((res = parse_subchunk_INFO(it, size)) != EXIT_SUCCESS) {
        return res;
      }
    } else if (is_ascii((const char *)it, size)) {
      print_raw((const char *)it, size);
    } else {
      printf("...");
    }
    printf("]\n");
    it += size;
  } // while

  return EXIT_SUCCESS;
}

int
main(int argc, char *args[]) {
  int fd;
  struct stat st;
  u8 *raw;
  int res = EXIT_FAILURE;

  if (argc != 2) {
    fprintf(stderr, "%s file\n", args[0]);
  }

  if ((fd = open(args[1], O_RDONLY)) < 0) {
    fprintf(stderr, "open(%s): %s\n", args[1], strerror(errno));
    return res;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(stderr, "fstat(%s): %s\n", args[1], strerror(errno));
    goto Lclose;
  }

  if ((raw = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0)) ==
      MAP_FAILED) {
    fprintf(stderr, "mmap(): %s\n", strerror(errno));
    goto Lclose;
  }

  res = parse_RIFF(raw, (size_t)st.st_size);

  munmap(raw, (size_t)st.st_size);
Lclose:
  close(fd);
  return res;
}

static const char *
AudioFormat(uint16_t format) {
  static char tmp[64];
  switch (format) {
  case 0x0000:
    return "Unknown";
    break;
  case 0x0001:
    return "PCM";
    break;
  case 0x0002:
    return "Microsoft ADPCM";
    break;
  case 0x0003:
    return "Microsoft IEEE float";
    break;
  case 0x0004:
    return "Compaq VSELP";
    break;
  case 0x0005:
    return "IBM CVSD";
    break;
  case 0x0006:
    return "ITU G.711 a-law";
    break;
  case 0x0007:
    return "ITU G.711 u-law";
    break;
  case 0x0008:
    return "Microsoft DTS";
    break;
  case 0x0009:
    return "DRM";
    break;
  case 0x000A:
    return "WMA 9 Speech";
    break;
  case 0x000B:
    return "Microsoft Windows Media RT Voice";
    break;
  case 0x0010:
    return "OKI-ADPCM";
    break;
  case 0x0011:
    return "Intel IMA/DVI-ADPCM";
    break;
  case 0x0012:
    return "Videologic Mediaspace ADPCM";
    break;
  case 0x0013:
    return "Sierra ADPCM";
    break;
  case 0x0014:
    return "Antex G.723 ADPCM";
    break;
  case 0x0015:
    return "DSP Solutions DIGISTD";
    break;
  case 0x0016:
    return "DSP Solutions DIGIFIX";
    break;
  case 0x0017:
    return "Dialogic OKI ADPCM";
    break;
  case 0x0018:
    return "Media Vision ADPCM";
    break;
  case 0x0019:
    return "HP CU";
    break;
  case 0x001A:
    return "HP Dynamic Voice";
    break;
  case 0x0020:
    return "Yamaha ADPCM";
    break;
  case 0x0021:
    return "SONARC Speech Compression";
    break;
  case 0x0022:
    return "DSP Group True Speech";
    break;
  case 0x0023:
    return "Echo Speech Corp.";
    break;
  case 0x0024:
    return "Virtual Music Audiofile AF36";
    break;
  case 0x0025:
    return "Audio Processing Tech.";
    break;
  case 0x0026:
    return "Virtual Music Audiofile AF10";
    break;
  case 0x0027:
    return "Aculab Prosody 1612";
    break;
  case 0x0028:
    return "Merging Tech. LRC";
    break;
  case 0x0030:
    return "Dolby AC2";
    break;
  case 0x0031:
    return "Microsoft GSM610";
    break;
  case 0x0032:
    return "MSN Audio";
    break;
  case 0x0033:
    return "Antex ADPCM";
    break;
  case 0x0034:
    return "Control Resources VQLPC";
    break;
  case 0x0035:
    return "DSP Solutions DIGIREAL";
    break;
  case 0x0036:
    return "DSP Solutions DIGIADPCM";
    break;
  case 0x0037:
    return "Control Resources CR10";
    break;
  case 0x0038:
    return "Natural MicroSystems VBX ADPCM";
    break;
  case 0x0039:
    return "Crystal Semiconductors IMA ADPCM";
    break;
  case 0x003A:
    return "Echo Speech ECHOSC3";
    break;
  case 0x003B:
    return "Rockwell ADPCM";
    break;
  case 0x003C:
    return "Rockwell DIGITALK";
    break;
  case 0x003D:
    return "Xebec Multimedia";
    break;
  case 0x0040:
    return "Antex G.721 ADPCM";
    break;
  case 0x0041:
    return "Antex G.728 CELP";
    break;
  case 0x0042:
    return "Microsoft MSG723";
    break;
  case 0x0043:
    return "IBM AVC ADPCM";
    break;
  case 0x0045:
    return "ITU-T G.726";
    break;
  case 0x0050:
    return "Microsoft MPEG";
    break;
  case 0x0051:
    return "RT23 or PAC";
    break;
  case 0x0052:
    return "InSoft RT24";
    break;
  case 0x0053:
    return "InSoft PAC";
    break;
  case 0x0055:
    return "MP3";
    break;
  case 0x0059:
    return "Cirrus";
    break;
  case 0x0060:
    return "Cirrus Logic";
    break;
  case 0x0061:
    return "ESS Tech. PCM";
    break;
  case 0x0062:
    return "Voxware Inc.";
    break;
  case 0x0063:
    return "Canopus ATRAC";
    break;
  case 0x0064:
    return "APICOM G.726 ADPCM";
    break;
  case 0x0065:
    return "APICOM G.722 ADPCM";
    break;
  case 0x0066:
    return "Microsoft DSAT";
    break;
  case 0x0067:
    return "Microsoft DSAT-DISPLAY";
    break;
  case 0x0069:
    return "Voxware Byte Aligned";
    break;
  case 0x0070:
    return "Voxware ACB";
    break;
  case 0x0071:
    return "Voxware AC10";
    break;
  case 0x0072:
    return "Voxware AC16";
    break;
  case 0x0073:
    return "Voxware AC20";
    break;
  case 0x0074:
    return "Voxware MetaVoice";
    break;
  case 0x0075:
    return "Voxware MetaSound";
    break;
  case 0x0076:
    return "Voxware RT29HW";
    break;
  case 0x0077:
    return "Voxware VR12";
    break;
  case 0x0078:
    return "Voxware VR18";
    break;
  case 0x0079:
    return "Voxware TQ40";
    break;
  case 0x007A:
    return "Voxware SC3";
    break;
  case 0x007B:
    return "Voxware SC3";
    break;
  case 0x0080:
    return "Soundsoft";
    break;
  case 0x0081:
    return "Voxware TQ60";
    break;
  case 0x0082:
    return "Microsoft MSRT24";
    break;
  case 0x0083:
    return "AT&T G.729A";
    break;
  case 0x0084:
    return "Motion Pixels MVI-MV12";
    break;
  case 0x0085:
    return "DataFusion G.726";
    break;
  case 0x0086:
    return "DataFusion GSM610";
    break;
  case 0x0088:
    return "Iterated Systems Audio";
    break;
  case 0x0089:
    return "Onlive";
    break;
  case 0x008A:
    return "Multitude, Inc. FT SX20";
    break;
  case 0x008B:
    return "Infocom IT’S A/S G.721 ADPCM";
    break;
  case 0x008C:
    return "Convedia G729";
    break;
  case 0x008D:
    return "Congruency, Inc. (not specified)";
    break;
  case 0x0091:
    return "Siemens SBC24";
    break;
  case 0x0092:
    return "Sonic Foundry Dolby AC3 APDIF";
    break;
  case 0x0093:
    return "MediaSonic G.723";
    break;
  case 0x0094:
    return "Aculab Prosody 8kbps";
    break;
  case 0x0097:
    return "ZyXEL ADPCM";
    break;
  case 0x0098:
    return "Philips LPCBB";
    break;
  case 0x0099:
    return "Studer Professional Audio Packed";
    break;
  case 0x00A0:
    return "Maiden PhonyTalk";
    break;
  case 0x00A1:
    return "Racal Recorder GSM";
    break;
  case 0x00A2:
    return "Racal Recorder G720.a";
    break;
  case 0x00A3:
    return "Racal G723.1";
    break;
  case 0x00A4:
    return "Racal Tetra ACELP";
    break;
  case 0x00B0:
    return "NEC AAC NEC Corporation";
    break;
  case 0x00FF:
    return "AAC";
    break;
  case 0x0100:
    return "Rhetorex ADPCM";
    break;
  case 0x0101:
    return "IBM u-Law";
    break;
  case 0x0102:
    return "IBM a-Law";
    break;
  case 0x0103:
    return "IBM ADPCM";
    break;
  case 0x0111:
    return "Vivo G.723";
    break;
  case 0x0112:
    return "Vivo Siren";
    break;
  case 0x0120:
    return "Philips Speech Processing CELP";
    break;
  case 0x0121:
    return "Philips Speech Processing GRUNDIG";
    break;
  case 0x0123:
    return "Digital G.723";
    break;
  case 0x0125:
    return "Sanyo LD ADPCM";
    break;
  case 0x0130:
    return "Sipro Lab ACEPLNET";
    break;
  case 0x0131:
    return "Sipro Lab ACELP4800";
    break;
  case 0x0132:
    return "Sipro Lab ACELP8V3";
    break;
  case 0x0133:
    return "Sipro Lab G.729";
    break;
  case 0x0134:
    return "Sipro Lab G.729A";
    break;
  case 0x0135:
    return "Sipro Lab Kelvin";
    break;
  case 0x0136:
    return "VoiceAge AMR";
    break;
  case 0x0140:
    return "Dictaphone G.726 ADPCM";
    break;
  case 0x0150:
    return "Qualcomm PureVoice";
    break;
  case 0x0151:
    return "Qualcomm HalfRate";
    break;
  case 0x0155:
    return "Ring Zero Systems TUBGSM";
    break;
  case 0x0160:
    return "Microsoft Audio1";
    break;
  case 0x0161:
    return "Windows Media Audio V2 V7 V8 V9 / DivX audio (WMA) / Alex AC3 "
           "Audio";
    break;
  case 0x0162:
    return "Windows Media Audio Professional V9";
    break;
  case 0x0163:
    return "Windows Media Audio Lossless V9";
    break;
  case 0x0164:
    return "WMA Pro over S/PDIF";
    break;
  case 0x0170:
    return "UNISYS NAP ADPCM";
    break;
  case 0x0171:
    return "UNISYS NAP ULAW";
    break;
  case 0x0172:
    return "UNISYS NAP ALAW";
    break;
  case 0x0173:
    return "UNISYS NAP 16K";
    break;
  case 0x0174:
    return "MM SYCOM ACM SYC008 SyCom Technologies";
    break;
  case 0x0175:
    return "MM SYCOM ACM SYC701 G726L SyCom Technologies";
    break;
  case 0x0176:
    return "MM SYCOM ACM SYC701 CELP54 SyCom Technologies";
    break;
  case 0x0177:
    return "MM SYCOM ACM SYC701 CELP68 SyCom Technologies";
    break;
  case 0x0178:
    return "Knowledge Adventure ADPCM";
    break;
  case 0x0180:
    return "Fraunhofer IIS MPEG2AAC";
    break;
  case 0x0190:
    return "Digital Theater Systems DTS DS";
    break;
  case 0x0200:
    return "Creative Labs ADPCM";
    break;
  case 0x0202:
    return "Creative Labs FASTSPEECH8";
    break;
  case 0x0203:
    return "Creative Labs FASTSPEECH10";
    break;
  case 0x0210:
    return "UHER ADPCM";
    break;
  case 0x0215:
    return "Ulead DV ACM";
    break;
  case 0x0216:
    return "Ulead DV ACM";
    break;
  case 0x0220:
    return "Quarterdeck Corp.";
    break;
  case 0x0230:
    return "I-Link VC";
    break;
  case 0x0240:
    return "Aureal Semiconductor Raw Sport";
    break;
  case 0x0241:
    return "ESST AC3";
    break;
  case 0x0250:
    return "Interactive Products HSX";
    break;
  case 0x0251:
    return "Interactive Products RPELP";
    break;
  case 0x0260:
    return "Consistent CS2";
    break;
  case 0x0270:
    return "Sony SCX";
    break;
  case 0x0271:
    return "Sony SCY";
    break;
  case 0x0272:
    return "Sony ATRAC3";
    break;
  case 0x0273:
    return "Sony SPC";
    break;
  case 0x0280:
    return "TELUM Telum Inc.";
    break;
  case 0x0281:
    return "TELUMIA Telum Inc.";
    break;
  case 0x0285:
    return "Norcom Voice Systems ADPCM";
    break;
  case 0x0300:
    return "Fujitsu FM TOWNS SND";
    break;
  case 0x0301:
    return "Fujitsu (not specified)";
    break;
  case 0x0302:
    return "Fujitsu (not specified)";
    break;
  case 0x0303:
    return "Fujitsu (not specified)";
    break;
  case 0x0304:
    return "Fujitsu (not specified)";
    break;
  case 0x0305:
    return "Fujitsu (not specified)";
    break;
  case 0x0306:
    return "Fujitsu (not specified)";
    break;
  case 0x0307:
    return "Fujitsu (not specified)";
    break;
  case 0x0308:
    return "Fujitsu (not specified)";
    break;
  case 0x0350:
    return "Micronas Semiconductors, Inc. Development";
    break;
  case 0x0351:
    return "Micronas Semiconductors, Inc. CELP833";
    break;
  case 0x0400:
    return "Brooktree Digital";
    break;
  case 0x0401:
    return "Intel Music Coder (IMC)";
    break;
  case 0x0402:
    return "Ligos Indeo Audio";
    break;
  case 0x0450:
    return "QDesign Music";
    break;
  case 0x0500:
    return "On2 VP7 On2 Technologies";
    break;
  case 0x0501:
    return "On2 VP6 On2 Technologies";
    break;
  case 0x0680:
    return "AT&T VME VMPCM";
    break;
  case 0x0681:
    return "AT&T TCP";
    break;
  case 0x0700:
    return "YMPEG Alpha (dummy for MPEG-2 compressor)";
    break;
  case 0x08AE:
    return "ClearJump LiteWave (lossless)";
    break;
  case 0x1000:
    return "Olivetti GSM";
    break;
  case 0x1001:
    return "Olivetti ADPCM";
    break;
  case 0x1002:
    return "Olivetti CELP";
    break;
  case 0x1003:
    return "Olivetti SBC";
    break;
  case 0x1004:
    return "Olivetti OPR";
    break;
  case 0x1100:
    return "Lernout & Hauspie";
    break;
  case 0x1101:
    return "Lernout & Hauspie CELP codec";
    break;
  case 0x1102:
    return "Lernout & Hauspie SBC codec";
    break;
  case 0x1103:
    return "Lernout & Hauspie SBC codec";
    break;
  case 0x1104:
    return "Lernout & Hauspie SBC codec";
    break;
  case 0x1400:
    return "Norris Comm. Inc.";
    break;
  case 0x1401:
    return "ISIAudio";
    break;
  case 0x1500:
    return "AT&T Soundspace Music Compression";
    break;
  case 0x181C:
    return "VoxWare RT24 speech codec";
    break;
  case 0x181E:
    return "Lucent elemedia AX24000P Music codec";
    break;
  case 0x1971:
    return "Sonic Foundry LOSSLESS";
    break;
  case 0x1979:
    return "Innings Telecom Inc. ADPCM";
    break;
  case 0x1C07:
    return "Lucent SX8300P speech codec";
    break;
  case 0x1C0C:
    return "Lucent SX5363S G.723 compliant codec";
    break;
  case 0x1F03:
    return "CUseeMe DigiTalk (ex-Rocwell)";
    break;
  case 0x1FC4:
    return "NCT Soft ALF2CD ACM";
    break;
  case 0x2000:
    return "FAST Multimedia DVM";
    break;
  case 0x2001:
    return "Dolby DTS (Digital Theater System)";
    break;
  case 0x2002:
    return "RealAudio 1 / 2 14.4";
    break;
  case 0x2003:
    return "RealAudio 1 / 2 28.8";
    break;
  case 0x2004:
    return "RealAudio G2 / 8 Cook (low bitrate)";
    break;
  case 0x2005:
    return "RealAudio 3 / 4 / 5 Music (DNET)";
    break;
  case 0x2006:
    return "RealAudio 10 AAC (RAAC)";
    break;
  case 0x2007:
    return "RealAudio 10 AAC+ (RACP)";
    break;
  case 0x2500:
    return "Reserved range to 0x2600 Microsoft";
    break;
  case 0x3313:
    return "makeAVIS (ffvfw fake AVI sound from AviSynth scripts)";
    break;
  case 0x4143:
    return "Divio MPEG-4 AAC audio";
    break;
  case 0x4201:
    return "Nokia adaptive multirate";
    break;
  case 0x4243:
    return "Divio G726 Divio, Inc.";
    break;
  case 0x434C:
    return "LEAD Speech";
    break;
  case 0x564C:
    return "LEAD Vorbis";
    break;
  case 0x5756:
    return "WavPack Audio";
    break;
  case 0x674F:
    return "Ogg Vorbis (mode 1)";
    break;
  case 0x6750:
    return "Ogg Vorbis (mode 2)";
    break;
  case 0x6751:
    return "Ogg Vorbis (mode 3)";
    break;
  case 0x676F:
    return "Ogg Vorbis (mode 1+)";
    break;
  case 0x6770:
    return "Ogg Vorbis (mode 2+)";
    break;
  case 0x6771:
    return "Ogg Vorbis (mode 3+)";
    break;
  case 0x7000:
    return "3COM NBX 3Com Corporation";
    break;
  case 0x706D:
    return "FAAD AAC";
    break;
  case 0x7A21:
    return "GSM-AMR (CBR, no SID)";
    break;
  case 0x7A22:
    return "GSM-AMR (VBR, including SID)";
    break;
  case 0xA100:
    return "Comverse Infosys Ltd. G723 1";
    break;
  case 0xA101:
    return "Comverse Infosys Ltd. AVQSBC";
    break;
  case 0xA102:
    return "Comverse Infosys Ltd. OLDSBC";
    break;
  case 0xA103:
    return "Symbol Technologies G729A";
    break;
  case 0xA104:
    return "VoiceAge AMR WB VoiceAge Corporation";
    break;
  case 0xA105:
    return "Ingenient Technologies Inc. G726";
    break;
  case 0xA106:
    return "ISO/MPEG-4 advanced audio Coding";
    break;
  case 0xA107:
    return "Encore Software Ltd G726";
    break;
  case 0xA109:
    return "Speex ACM Codec xiph.org";
    break;
  case 0xDFAC:
    return "DebugMode SonicFoundry Vegas FrameServer ACM Codec";
    break;
  case 0xE708:
    return "Unknown";
    break;
  case 0xF1AC:
    return "Free Lossless Audio Codec FLAC";
    break;
  case 0xFFFE:
    return "Extensible";
    break;
  case 0xFFFF:
    return "Development";
    break;
  }

  sprintf(tmp, "%d", format);
  return tmp;
}
