// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 libplist link/roundtrip smoke probe; no networking.
#include <plist/plist.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    plist_t root = plist_new_dict();
    if (!root) return 1;
    plist_dict_set_item(root, "device", plist_new_string("MirrorPSX"));
    plist_dict_set_item(root, "session", plist_new_uint(47));
    char* xml = NULL;
    uint32_t size = 0;
    plist_to_xml(root, &xml, &size);
    if (!xml || size == 0) { plist_free(root); return 2; }
    plist_t roundtrip = NULL;
    plist_from_xml(xml, size, &roundtrip);
    free(xml);
    plist_free(root);
    if (!roundtrip) return 3;
    plist_t device = plist_dict_get_item(roundtrip, "device");
    char* value = NULL;
    plist_get_string_val(device, &value);
    const int result = !value || strcmp(value, "MirrorPSX") != 0;
    free(value);
    plist_free(roundtrip);
    return result ? 4 : 0;
}
