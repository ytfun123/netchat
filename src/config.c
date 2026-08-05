#include "config.h"

#include <fileioc.h>
#include <string.h>

#define CONFIG_VAR_NAME "NCCFG"
#define CONFIG_MAGIC 0xC6 /* bumped when the on-disk struct layout changes */

/* On-disk layout (all in one AppVar, written as raw bytes): magic byte,
 * then the config struct verbatim. The magic byte lets us tell "no config
 * yet" apart from "corrupted / from an incompatible future version".
 *
 * The AppVar is kept in the flash archive (never plain RAM) so a simple
 * RAM reset can't wipe the username/password/P2P ID and force the setup
 * wizard to run again -- the only way to truly reset is to delete the
 * NCCFG AppVar (or clear the archive) on purpose. */
bool config_Load(netchat_config_t *config) {
    uint8_t handle = ti_Open(CONFIG_VAR_NAME, "r");
    if (!handle) return false;

    uint8_t magic = 0;
    ti_Read(&magic, 1, 1, handle);

    bool ok = false;
    if (magic == CONFIG_MAGIC) {
        size_t read = ti_Read(config, sizeof(netchat_config_t), 1, handle);
        ok = (read == 1);
    }

    /* Self-heal: if the var somehow ended up in RAM, move it back to the
     * archive so a later RAM reset can't silently wipe it. */
    if (!ti_IsArchived(handle))
        ti_SetArchiveStatus(true, handle);

    ti_Close(handle);
    return ok;
}

bool config_Save(const netchat_config_t *config) {
    uint8_t handle = ti_Open(CONFIG_VAR_NAME, "w+");
    if (!handle) return false;

    uint8_t magic = CONFIG_MAGIC;
    ti_Write(&magic, 1, 1, handle);
    size_t written = ti_Write(config, sizeof(netchat_config_t), 1, handle);

    ti_SetArchiveStatus(true, handle);

    ti_Close(handle);
    return written == 1;
}
