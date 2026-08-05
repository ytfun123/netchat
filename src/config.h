#ifndef NETCHAT_CONFIG_H
#define NETCHAT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define USERNAME_MAX 16
#define PASSWORD_MAX 16
#define TOKEN_MAX 36 /* UUID string length */
#define PEER_ID_MAX 16 /* short PeerJS ID (5 chars, kept generous) */

typedef struct {
    char username[USERNAME_MAX + 1];
    char password[PASSWORD_MAX + 1]; /* transmitted once at claim/register
                                       * time, over the local USB link only
                                       * -- never stored server-side in
                                       * plaintext, see api/proxy.js */
    char device_token[TOKEN_MAX + 1]; /* empty until the site assigns one */
    char peer_id[PEER_ID_MAX + 1]; /* P2P ID, empty until the site assigns one */
    uint8_t color_index; /* index into the palette in wizard.c */
} netchat_config_t;

bool config_Load(netchat_config_t *config);
bool config_Save(const netchat_config_t *config);

#endif
