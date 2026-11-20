#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Validate the owned save observer's full published chain and reconstruct its
// exact original 64-byte body. Buffers must be read from checked RX mappings.
inline bool owned_save_pin(const uint8_t live[64], const uint8_t relay[14], const uint8_t tramp[22], uintptr_t site, uintptr_t page, uintptr_t original, uintptr_t proxy) {
 if (!live || !relay || !tramp || !site || !page || !proxy || (page & 0x3fff) || page > UINTPTR_MAX-0x4000 || site > UINTPTR_MAX-64 || original != page+0x40) return false;
 static const uint8_t prefix[8]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56};
 static const uint8_t absjmp[6]={0xff,0x25,0,0,0,0};
 if (live[0]!=0xe9 || live[5]!=0x90 || live[6]!=0x90 || live[7]!=0x90 || memcmp(tramp,prefix,8) || memcmp(tramp+8,absjmp,6) || memcmp(relay,absjmp,6)) return false;
 int32_t displacement; memcpy(&displacement,live+1,4);
 uintptr_t next=site+5, destination;
 if(displacement<0){uintptr_t d=(uintptr_t)(-(int64_t)displacement);if(next<d)return false;destination=next-d;}else{if(next>UINTPTR_MAX-(uint32_t)displacement)return false;destination=next+(uint32_t)displacement;}
 uintptr_t relay_target=0, return_target=0;memcpy(&relay_target,relay+6,8);memcpy(&return_target,tramp+14,8);
 if(destination!=page || relay_target!=proxy || return_target!=site+8)return false;
 uint32_t hash=2166136261u;for(size_t i=0;i<64;i++){hash^=i<8?tramp[i]:live[i];hash*=16777619u;}
 return hash==0x838e1ce9u;
}
