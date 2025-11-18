/* tinyfiledialogs - v3.24 [minimal header, fetched]
   For full license and updates see: https://sourceforge.net/projects/tinyfiledialogs/ */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
extern const char * tinyfd_selectFolderDialog(
    char const * const aTitle, /* 0 or "" */
    char const * const aDefaultPath /* 0 or "" */);
#ifdef __cplusplus
}
#endif
