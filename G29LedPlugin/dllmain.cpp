#include "pch.h"
#include <stdio.h>
#include <share.h>

#include "log.h"
#include "poller.h"
#include "scsutil.h"
#include "g29led.h"
#include "truck.h"

#define UNUSED(x)
#define StdCall __stdcall

#pragma comment( linker, "/export:scs_telemetry_init" )
#pragma comment( linker, "/export:scs_telemetry_shutdown" )

#define MINATSVER SCS_TELEMETRY_ATS_GAME_VERSION_1_05
#define MINETS2VER SCS_TELEMETRY_EUT2_GAME_VERSION_1_18

#ifdef x64

// TODO: appropriately get the module addresses to determine de module address space
#define ROMEM_MIN_ADDR 0x00007ff000000000
#define ROMEM_MAX_ADDR 0x00007fff00000000

#define BETWEEN(x,low, hi) x >= low && x <= hi
#define NOT_BETWEEN(x,low, hi) x < low || x > hi

struct ptrpair_t {
    uintptr_t romem;
    uintptr_t rwmem;
};

struct int_float_pair_t {
    scs_s32_t intfld;
    scs_float_t floatfld;
};

struct ptrp_lens_t {
    ptrpair_t addrs;
    scs_u64_t lens[2];
};

struct truck_info_with_capacity_t {
    // searching memory, many more pointers are set to an additional
    // romem-uint-zero structure (16 bytes total), thus prepending this to
    // the structure increases the odds on finding the structure faster.
    // This applies for all 3 occurrences of this structure in game memory
    // (as 1.47.x), being:
    // struct #1: 14 pointers to -16-byte; 01 pointer to 0-byte
    // structs #2 and #3: 2 pointers to -16-byte; 01 pointer to 0-byte
    // the "uint" part of the structure seems to change from 00 00 00 04 to
    // 06 00 00 a4, at least in the few checks performed. The latter usually
    // has rw-pointers after ro-pointers where the former has zero-pointers
    // where those rw-pointers would go.
    uintptr_t prefield01_romem;
    scs_u32_t prefield02_nznum;
    scs_u32_t prefield03_znum;

    ptrp_lens_t f01_04_addrs;
    uintptr_t f05_rwmem;
    // 06: e.g 89 ce 00 00
    // 07: 0.0-1.0; e.g. 0.50
    // 08: e.g 1007
    // 09: 0.0-1.0
    // 10: e.g. 1990
    // 11: e.g. 72.15
    // 12: e.g. 2350
    // 13: 0.0-1.0, e.g. 0.54
    int_float_pair_t f06_f13_pairs[4];
    ptrpair_t f14_15_addrs;
    scs_u32_t f16_17_num[2]; // e.g. 16:16; 17:32
    int_float_pair_t f18_19_nums; // e.g. 18:inintelligible; 19: 38.44
    // resp "lens", 3, 6, 6, 6
    ptrp_lens_t f20_35_data[4];
    scs_float_t f36_39_num[4]; // e.g.: -1.0, 0.01, -1.0, -1.0
    scs_u64_t f40_data; // inintelligible (993454364 int at the time of writing)
    scs_float_t f41_num; // e.g. 7856.00 (don't know where this came from)
    scs_float_t f42_tank_cap; // e.g. 681.40 (our value!)
    scs_float_t f43_adblue_cap; // e.g. 80.0 (can get from telemetry to double-check)
    scs_float_t f44_num; // e.g. 0.0, 0.48, -25.95 (no idea)
    scs_u32_t f45_46_num[2]; // inintelligible; both equal
    scs_u32_t f47_num; // inintelligible (99
    scs_float_t f48_tank_fill; // e.g. 0.21 (21%) fuel
    scs_float_t f49_adbl_fill; // e.g. 0.23 (23%), usually higher than fuel fill unless both 100%
    scs_float_t f50_num; // e.g. 2.10 (10x tank_fill?)
    scs_float_t f51_num; // e.g. 0.75 -- no idea

    // From this point on, the values are very unreliable between structures found
    //scs_float_t f52_num; // e.g. 0.12? -- no idea, and might be int, depending where the structure is found
    //ptrp_lens_t f53_56_data; // e.g. 7
    //scs_s32_t f57_num; // inintelligible
    //scs_float_t f58_64num[7]; // resp: 0.08, -0.10, -5.0, 0.15, -0.10, 5.0, -10.0
    //uintptr_t f65_nulptr; // 0x00
};

static uintptr_t min_ptr = 0x00, max_ptr = 0x00; // this may change every game run

bool validate_pointer_pair(ptrpair_t* pair, bool rwmem_zero = false) {
    if (NOT_BETWEEN(pair->romem, ROMEM_MIN_ADDR, ROMEM_MAX_ADDR)) return false;
    else if (rwmem_zero) {
        if (pair->rwmem != 0) return false;
    } else if (NOT_BETWEEN(pair->rwmem, min_ptr, max_ptr)) return false;
    return true;
}

bool validate_ptrplens(ptrp_lens_t* frame, bool rwmem_zero = false) {
    if (!validate_pointer_pair(&(frame->addrs), rwmem_zero)) return false;

    if (rwmem_zero) {
        // When the read-write memory address is null, the first 8-byte length
        // must be zero, but the second one may be zero or a number.
        if (frame->lens[0] != 0 || frame->lens[1] > 10000) return false;
    } else {
        // When there's an address to read-write memory, then both lengths
        // should be a non-zero and the same value.
        if (frame->lens[0] != frame->lens[1] || frame->lens[0] == 0 || frame->lens[0] > 10000) return false;
    }
    return true;
}

bool validate_main_struct(uintptr_t* pointer_to_truck_structure, float adblue_cap) {
    if (IsBadReadPtr(pointer_to_truck_structure, sizeof(truck_info_with_capacity_t))) {
        //log("Structure candidate unreadable. Address: 0x%p - size (bytes): %llu", pointer_to_truck_structure, sizeof(truck_info_with_capacity_t));
        return false;
    }
    truck_info_with_capacity_t* data = (truck_info_with_capacity_t*)pointer_to_truck_structure;
    bool zero_rw_pointers = data->f01_04_addrs.addrs.rwmem == 0;

    // Validate the target value at once, and then check if the rest of the structure is sane
    if (NOT_BETWEEN(data->f42_tank_cap, 30.0f, 5000.0f)) {
        //log("Structure doesn't hold a sane tank capacity value.");
        return false;
    } else if (data->f43_adblue_cap != adblue_cap) {
        //log("AdBlue capacity field doesn't match what was provided by telemetry (provided: %1.2f / 0x%08llx; checked: 0x%08llx).", adblue_cap, adblue_cap, data->f43_adblue_cap);
        return false;
    } else if (NOT_BETWEEN(data->prefield01_romem, ROMEM_MIN_ADDR, ROMEM_MAX_ADDR) || data->prefield02_nznum == 0 || data->prefield03_znum != 0) {
        //log("Pre-Fields 1 & 2 don't match pattern: [ r/o mem addr : non-zero uint : zero uint ] pattern: [ 0x%08llx : 0x%04x : 0x%04x ]",
        //    data->prefield01_romem, data->prefield02_nznum, data->prefield03_znum);
        return false;
    } else if (!validate_ptrplens(&(data->f01_04_addrs), zero_rw_pointers)) {
        //log("Fields 1 thru 4 didn't pass.");
        return false;
    } else if (NOT_BETWEEN(data->f05_rwmem, min_ptr, max_ptr)) {
        // From this point on, it already matched a lot and should be correct,
        // so if it doesn't, log.
        log("Field 5 didn't pass.");
        return false;
    } else if (NOT_BETWEEN(data->f06_f13_pairs[0].intfld, 0, 100000) ||
               NOT_BETWEEN(data->f06_f13_pairs[0].floatfld, 0.0f, 1.0f) ||
               NOT_BETWEEN(data->f06_f13_pairs[1].intfld, 0, 10000) ||
               NOT_BETWEEN(data->f06_f13_pairs[1].floatfld, 0.0f, 10.0f) ||
               NOT_BETWEEN(data->f06_f13_pairs[2].intfld, 0, 20000) ||
               NOT_BETWEEN(data->f06_f13_pairs[2].floatfld, 0.0f, 10000.0f) ||
               NOT_BETWEEN(data->f06_f13_pairs[3].intfld, 0, 20000) ||
               NOT_BETWEEN(data->f06_f13_pairs[3].floatfld, 0.0f, 1.0f)) {
        log("One or more fields between 6 and 13 didn't pass.\n  "
            "0i[0:100k=%i] 0f[0:1=%1.4f]\n  "
            "1i[0:10k=%i] 1f[0:10=%1.4f]\n  "
            "2i[0:20k=%i] 2f[0:10k=%1.4f]\n  "
            "3i[0:20k=%i] 3f[0:1=%1.4f]",
            data->f06_f13_pairs[0].intfld, data->f06_f13_pairs[0].floatfld,
            data->f06_f13_pairs[1].intfld, data->f06_f13_pairs[1].floatfld,
            data->f06_f13_pairs[2].intfld, data->f06_f13_pairs[2].floatfld,
            data->f06_f13_pairs[3].intfld, data->f06_f13_pairs[3].floatfld);
        return false;
    } else if (!validate_pointer_pair(&(data->f14_15_addrs), zero_rw_pointers)) {
        log("[0x%p] Fields 14 (0x%p) and/or 15 (0x%p) didn't pass.", pointer_to_truck_structure, data->f14_15_addrs.romem, data->f14_15_addrs.rwmem);
        return false;
    } else if (data->f16_17_num[0] > 10000 || data->f16_17_num[1] > 10000) {
        log("[0x%p] Fields 16 (%lu) and/or 17 (%lu) didn't pass.", pointer_to_truck_structure, data->f16_17_num[0], data->f16_17_num[1]);
        return false;
    } else if (NOT_BETWEEN(data->f18_19_nums.floatfld, 0.0f, 10000.0f)) {
        log("[0x%p] Field 19 didn't pass.", pointer_to_truck_structure);
        return false;
    } else if (!validate_ptrplens(&(data->f20_35_data[0]), zero_rw_pointers) ||
               !validate_ptrplens(&(data->f20_35_data[1]), zero_rw_pointers) ||
               !validate_ptrplens(&(data->f20_35_data[2]), zero_rw_pointers) ||
               !validate_ptrplens(&(data->f20_35_data[3]), zero_rw_pointers)) {
        log("[0x%p] Fields 20 to 35 didn't pass.", pointer_to_truck_structure);
        log("[20ro:0x%p; 21rw:0x%p; 22u:%llu; 23u:%llu] %s",
            data->f20_35_data[0].addrs.romem, data->f20_35_data[0].addrs.rwmem, data->f20_35_data[0].lens[0], data->f20_35_data[0].lens[1],
            validate_ptrplens(&(data->f20_35_data[0]), zero_rw_pointers) ? "v" : "x");
        log("[24ro:0x%p; 25rw:0x%p; 26u:%llu; 27u:%llu] %s",
            data->f20_35_data[1].addrs.romem, data->f20_35_data[1].addrs.rwmem, data->f20_35_data[1].lens[0], data->f20_35_data[1].lens[1],
            validate_ptrplens(&(data->f20_35_data[1]), zero_rw_pointers) ? "v" : "x");
        log("[28ro:0x%p; 29rw:0x%p; 30u:%llu; 31u:%llu] %s",
            data->f20_35_data[2].addrs.romem, data->f20_35_data[2].addrs.rwmem, data->f20_35_data[2].lens[0], data->f20_35_data[2].lens[1],
            validate_ptrplens(&(data->f20_35_data[2]), zero_rw_pointers) ? "v" : "x");
        log("[32ro:0x%p; 33rw:0x%p; 34u:%llu; 35u:%llu] %s",
            data->f20_35_data[3].addrs.romem, data->f20_35_data[3].addrs.rwmem, data->f20_35_data[3].lens[0], data->f20_35_data[3].lens[1],
            validate_ptrplens(&(data->f20_35_data[3]), zero_rw_pointers) ? "v" : "x");
        return false;
    } else if (NOT_BETWEEN(data->f36_39_num[0], -1.0f, 1.0f) || NOT_BETWEEN(data->f36_39_num[1], -1.0f, 1.0f) || NOT_BETWEEN(data->f36_39_num[2], -1.0f, 1.0f) || NOT_BETWEEN(data->f36_39_num[3], -1.0f, 1.0f)) {
        log("[0x%p] Fields 36 to 39 didn't pass.", pointer_to_truck_structure);
        return false;
    } else if (NOT_BETWEEN(data->f41_num, 0.0f, 100000.0f)) {
        log("[0x%p] Field 41 didn't pass.", pointer_to_truck_structure);
        return false;
    } else if (NOT_BETWEEN(data->f48_tank_fill, 0.0f, 1.0f)) {
        log("[0x%p] Field 48, tank fill, is not a number between 0.0 and 1.0.", pointer_to_truck_structure);
        return false;
    } else if (NOT_BETWEEN(data->f49_adbl_fill, 0.0f, 1.0f)) {
        log("[0x%p] Field 49, AdBlue fill, is not a number between 0.0 and 1.0.", pointer_to_truck_structure);
        return false;
    }
    // fields #42 & #43 checked first thing. From this point on, it's not very
    // meaningful to check every member left, but the tests will be left here
    // just in case:
    return true;

    if (NOT_BETWEEN(data->f44_num, -1000.0f, 1000.0f)) {
        log("Field 44 didn't pass.");
    } else if (data->f45_46_num[0] != data->f45_46_num[1]) {
        log("Fields 45 and 46 don't hold the same value.");
        return false;
        // Field 47 is not known
    } else if (NOT_BETWEEN(data->f48_tank_fill, 0.0f, 1.0f)) {
        log("Field 48, tank fill, is not a number between 0.0 and 1.0.");
        return false;
    } else if (NOT_BETWEEN(data->f49_adbl_fill, 0.0f, 1.0f)) {
        log("Field 49, AdBlue fill, is not a number between 0.0 and 1.0.");
        return false;
    } else if (NOT_BETWEEN(data->f50_num, 0.0f, 1000.0f)) {
        log("Field 50, didn't pass.");
        return false;
    } else if (NOT_BETWEEN(data->f51_num, 0.0f, 1.0f)) {
        log("Field 51, didn't pass.");
        return false;
    }

    return true;
}

// There are many fewer pointers pointing to this structure, and only one copy of it
// throughout game memory; and it doesn't have any other meaningful data other than
// the tank capaticy itself, but this could prove useful when/if the other structures
// become unreliable across binary changes or versions.
bool validate_alt_struct(uintptr_t* pointer_to_tank_structure) {
    uintptr_t ptr_to_tank_cap_val = (uintptr_t)*pointer_to_tank_structure;
    if (IsBadReadPtr((void*)ptr_to_tank_cap_val, 53 * sizeof(uintptr_t))) {
        log("Pointer to possible actual tank capacity structure is not readable. Aborting search.");
        log("Structure address: %p - size (bytes): %llu", ptr_to_tank_cap_val, 53 * sizeof(uintptr_t));
        return false;
    }

    // This is a potential pointer to the structure.
    uintptr_t* tank_cap_struct = (uintptr_t*)ptr_to_tank_cap_val;
    float tank_cap = *(float*)(tank_cap_struct + 53);
    if (tank_cap > 200.0f && tank_cap < 5000.0f) {
        log("pointer candidate: %p => %p (cap: %1.4f)", (uintptr_t)pointer_to_tank_structure, ptr_to_tank_cap_val, tank_cap);
        bool match = true;
        unsigned short i;
        unsigned short zeroOffsets[] = { 9, 10, 11, 19, 20, 21, 27, 28, 29, 31, 32, 33, 40, 43, 46, 52 };
        unsigned short len = sizeof(zeroOffsets) / sizeof(unsigned short);
        unsigned short offset;
        for (i = 0; i < len; i++) {
            offset = zeroOffsets[i];
            if (*(tank_cap_struct + offset) != 0) {
                log("skipping, tank_cap_struct+%u is not zero: %p => 0x%016llx", offset, tank_cap_struct + offset, *(tank_cap_struct + offset));
                return false;
            }
        }

        // Commented ones proved to be unreliable.
        unsigned short rwPointerOffsets[] = { /*3, 6,*/ 15, 23, 36, 48 };
        len = sizeof(rwPointerOffsets) / sizeof(unsigned short);
        for (i = 0; i < len; i++) {
            offset = rwPointerOffsets[i];
            if (*(tank_cap_struct + offset) < min_ptr || *(tank_cap_struct + offset) > max_ptr) {
                log("skipping, tank_cap_struct+%u not a pointer to read-write memory space: %p => 0x%016llx", offset, tank_cap_struct + offset, *(tank_cap_struct + offset));
                return false;
            }
        }

        unsigned short roPointerOffsets[] = { 0, 2, 5, 8, 14, 18, 22, 26, 30, 35, 38, 39, 41, 42, 44, 45, 47 };
        len = sizeof(roPointerOffsets) / sizeof(unsigned short);
        for (i = 0; i < len; i++) {
            offset = roPointerOffsets[i];
            if (*(tank_cap_struct + offset) < 0x00007ff000000000 || *(tank_cap_struct + offset) >= 0x00007fff00000000) {
                log("skipping, tank_cap_struct+%u not a pointer to read-only memory space: %p => 0x%016llx", offset, tank_cap_struct + offset, *(tank_cap_struct + offset));
                return false;
            }
        }

        log("Looks like the structure was found. Tank cap found: %1.4f.", tank_cap);
        return true;
    } else return false;
}
#endif // x64

SCSAPI_VOID telemetry_pause(const scs_event_t event, const void* const UNUSED(event_info), const scs_context_t UNUSED(context)) {
    truck_data.paused = (event == SCS_TELEMETRY_EVENT_paused);
    if (truck_data.paused) {
        log("Realtime data resumed.");
    } else {
        log("Realtime data interrupted.");
    }
}

SCSAPI_VOID telemetry_configuration(const scs_event_t event, const void* const event_info, const scs_context_t UNUSED(context))
{
    // We currently only care for the truck telemetry info.

    const struct scs_telemetry_configuration_t* const info = static_cast<const scs_telemetry_configuration_t*>(event_info);
#ifdef _DEBUGx
    scs_value_dvector_t dpos;
    scs_value_fvector_t pos;
    scs_value_euler_t orient;
    log("Dumping all configuration values received for id: %s (%p)", info->id, info);
    for (const scs_named_value_t* current = info->attributes; current->name; ++current) {
#define logType(replacer, type) log("[current:%p,current->value.value_" #type ":%p]\n    #%u %s (" #type "): " replacer, current, current->value.value_ ## type, current->index, current->name, current->value.value_ ## type.value); break
        switch (current->value.type) {
        case SCS_VALUE_TYPE_bool:
            log("#%u %s (bool): %s", current->index, current->name, current->value.value_bool.value ? "true" : "false");
            break;
        case SCS_VALUE_TYPE_double:
            logType("%.4f", double);
        case SCS_VALUE_TYPE_dplacement:
            // dplace.position{x{dbl}, y{dbl}, z{dbl}}, dplace.orient{heading{f}, pitch{f}, roll{f}}, dplace._padding{int}
            dpos = current->value.value_dplacement.position;
            orient = current->value.value_dplacement.orientation;
            log("#%u %s (dplacement): (%.4f,%.4f,%.4f) [head:%.2f, pitch:%.2f, roll:%.2f] pad:%i",
                current->index, current->name,
                dpos.x, dpos.y, dpos.z, orient.heading, orient.pitch, orient.roll,
                current->value.value_dplacement._padding);
            break;
        case SCS_VALUE_TYPE_dvector:
            dpos = current->value.value_dvector;
            log("#%u %s (dvector): (%.4f,%.4f,%.4f)", current->index, current->name, dpos.x, dpos.y, dpos.z);
            break;
        case SCS_VALUE_TYPE_euler:
            orient = current->value.value_euler;
            log("#%u %s (euler): head:%.2f, pitch:%.2f, roll:%.2f", current->index, current->name, orient.heading, orient.pitch, orient.roll);
            break;
        case SCS_VALUE_TYPE_float:
            logType("%.2f", float);
        case SCS_VALUE_TYPE_fplacement:
            // fplace.position{x{f}, y{f}, z{f}}, fplace.orient{heading{f}, pitch{f}, roll{f}}
            pos = current->value.value_fplacement.position;
            orient = current->value.value_fplacement.orientation;
            log("#%u %s (dplacement): (%.2f,%.2f,%.2f) [head:%.2f, pitch:%.2f, roll:%.2f]", current->index, current->name,
                pos.x, pos.y, pos.z, orient.heading, orient.pitch, orient.roll);
            break;
        case SCS_VALUE_TYPE_fvector:
            pos = current->value.value_fvector;
            log("#%u %s (dvector): (%.2f,%.2f,%.2f)", current->index, current->name, pos.x, pos.y, pos.z);
            break;
        case SCS_VALUE_TYPE_INVALID:
            log("#%u %s (invalid): N/A", current->index, current->name);
            break;
            // case SCS_VALUE_TYPE_LAST: // synonym for s64 below
        case SCS_VALUE_TYPE_s32:
            logType("%i", s32);
        case SCS_VALUE_TYPE_s64:
            logType("%l", s64);
        case SCS_VALUE_TYPE_string:
            //log("[%p,%p,%p,%p]#%i (string pointer)", current->value, current->value.value_string, current->value.value_string.value, &(current->value.value_string.value), current->index);
            logType("%s", string);
        case SCS_VALUE_TYPE_u32:
            logType("%u", u32);
        case SCS_VALUE_TYPE_u64:
            logType("%lu", u64);
        default:
            log("- %s (unknown): N/A", current->name);
        }
    }
#endif
    if (strcmp(info->id, SCS_TELEMETRY_CONFIG_truck) != 0) {
        return;
    }

    const scs_named_value_t* const fuel_capacity_cfg = find_attribute(*info, SCS_TELEMETRY_CONFIG_ATTRIBUTE_fuel_capacity, SCS_U32_NIL, SCS_VALUE_TYPE_float);

    truck_data_access.lock();
    if (fuel_capacity_cfg) {
        truck_data.fuel_max = fuel_capacity_cfg->value.value_float.value;
    } else {
        truck_data.fuel_max = 200.0f;
    }
    truck_data_access.unlock();

#ifdef x64
    uintptr_t ref_ptr = (uintptr_t)info->attributes;

    const scs_named_value_t* const adblue_cap_cfg = find_attribute(*info, SCS_TELEMETRY_CONFIG_ATTRIBUTE_adblue_capacity, SCS_U32_NIL, SCS_VALUE_TYPE_float);

    float adblue_cap = adblue_cap_cfg ? adblue_cap_cfg->value.value_float.value : 80.0f;

    // TODO: Different math needed for 32-bit builds!

    // pointer has enough room for our validation, to tell whether a value
    // is within our current memory address chunk.
    min_ptr = ref_ptr - 0x1000000000;
    max_ptr = ref_ptr + 0x1000000000;

    // The search pointer limit should be narrower so that the game doesn't
    // hang for too long searching for the value.
    uintptr_t min_search_ptr = ref_ptr - 0x2000000;
    uintptr_t max_search_ptr = ref_ptr + 0x2000000;

    log("Ref ptr: %p; address interval: [0x%016llx:0x%016llx]", ref_ptr, min_ptr, max_ptr);

    bool search_up = true;
    unsigned long long amplitude = 1, checkcnt = 0;
    uintptr_t* cur_ptr = (uintptr_t*)ref_ptr;
    uintptr_t cur_addr = 0; // = (uintptr_t)cur_ptr;
    uintptr_t cur_val = 0; // = (uintptr_t)*cur_ptr;
    unsigned int guard = 0;

    log("Searching game memory for actual truck tank capacity info.");
    while (guard++ < 0x1fffffff) {
        if (search_up) {
            cur_ptr = (uintptr_t*)ref_ptr - amplitude;
            search_up = false;
            if ((uintptr_t)cur_ptr < min_search_ptr) {
                log("Reached upper search memory boundary space (0x%p). Aborting search.", min_search_ptr);
                break;
            }
        } else {
            cur_ptr = (uintptr_t*)ref_ptr + amplitude;
            amplitude++;
            search_up = true;
            if ((uintptr_t)cur_ptr > max_search_ptr) {
                log("Reached lower search memory boundary space (0x%p). Aborting search.", max_search_ptr);
                break;
            }
        }

        if (IsBadReadPtr(cur_ptr, sizeof(uintptr_t))) continue;

        cur_addr = (uintptr_t)cur_ptr;
        cur_val = (uintptr_t)*cur_ptr;

        // If the "pointer" has an address pointing within our read-write memory space
        if (BETWEEN(cur_val, min_ptr, max_ptr)) {
            checkcnt++;
            //log("cur_ptr:0x%08llx(%p), cur_addr:0x%08llx(%p), cur_val:0x%08llx(%p)", cur_ptr, cur_ptr, cur_addr, cur_addr, cur_val, cur_val);
            if (validate_main_struct((uintptr_t*)cur_val, adblue_cap)) {
                truck_info_with_capacity_t* truck_info = (truck_info_with_capacity_t*)cur_val;
                log("Found truck structure address at 0x%08llx (pointer at 0x%08llx, actual value at 0x%08llx). Value: %1.4f",
                    cur_val, cur_ptr, &(truck_info->f42_tank_cap), truck_info->f42_tank_cap);
                truck_data_access.lock();
                truck_data.fuel_max = truck_info->f42_tank_cap;
                truck_data_access.unlock();
                break;
            }
            /*if (validate_alt_struct((uintptr_t*)cur_val)) {
                truck_data_access.lock();
                truck_data.fuel_max = *(float*)((uintptr_t*)cur_val + 53);
                truck_data_access.unlock();
                break;
            }*/
        }
    };
    log("Search finished. Searched %llu potential pointers, over a distance of %llu 8-byte segments %s from the reference address [0x%08llx].",
        checkcnt, amplitude, search_up ? "down" : "up", ref_ptr);
#endif // x64

    log("Received new truck configuration: fuel capacity: %1.2f", truck_data.fuel_max);
}

/**
 * @brief Telemetry API initialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params)
{
    if (version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;

    const scs_telemetry_init_params_v101_t* const version_params = static_cast<const scs_telemetry_init_params_v101_t*>(params);

    const scs_sdk_init_params_v100_t* common = &version_params->common;
    game_log = common->log;

    log("Initializing");

    const char* game_name;

    if (strcmp(common->game_id, SCS_GAME_ID_EUT2) == 0) {
        game_name = "Euro Truck Simulator 2";
        const scs_u32_t MINIMAL_VERSION = MINETS2VER;
        if (common->game_version < MINIMAL_VERSION)
            logWarn("WARNING: Too old version of the game, some features might behave incorrectly");

        // Future versions are fine as long the major version is not changed.
        const scs_u32_t IMPLEMENTED_VERSION = SCS_TELEMETRY_EUT2_GAME_VERSION_CURRENT;
        if (SCS_GET_MAJOR_VERSION(common->game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION))
            logWarn("WARNING: This plugin was implemented for ETS2 version %u.%u. Current version is %u.%u. Some or all features might not work.",
                SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION), SCS_GET_MINOR_VERSION(IMPLEMENTED_VERSION),
                SCS_GET_MAJOR_VERSION(common->game_version), SCS_GET_MINOR_VERSION(common->game_version));
    } else if (strcmp(common->game_id, SCS_GAME_ID_ATS) == 0) {
        game_name = "American Truck Simulator";
        const scs_u32_t MINIMAL_VERSION = MINATSVER;
        if (common->game_version < MINIMAL_VERSION)
            logWarn("WARNING: Too old version of the game, some features might behave incorrectly");

        // Future versions are fine as long the major version is not changed.
        const scs_u32_t IMPLEMENTED_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_CURRENT;
        if (SCS_GET_MAJOR_VERSION(common->game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION))
            logWarn("WARNING: This plugin was implemented for ATS version %u.%u. Current version is %u.%u. Some or all features might not work.",
                SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION), SCS_GET_MINOR_VERSION(IMPLEMENTED_VERSION),
                SCS_GET_MAJOR_VERSION(common->game_version), SCS_GET_MINOR_VERSION(common->game_version));
    } else {
        game_name = "Unsupported game (!)";
        logWarn("WARNING: Unsupported game, aborting initialization.");
        return SCS_RESULT_unsupported;
    }

    log("Game session: %s (%s) v%u.%u", game_name, common->game_id,
        SCS_GET_MAJOR_VERSION(common->game_version), SCS_GET_MINOR_VERSION(common->game_version));

    // Register for events. Note that failure to register those basic events
    // likely indicates invalid usage of the api or some critical problem. As the
    // example requires all of them, we can not continue if the registration fails.

    const bool events_registered =
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, NULL) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, NULL) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_configuration, telemetry_configuration, NULL) == SCS_RESULT_ok)
        ;
    if (!events_registered) {
        // Registrations created by unsuccessfull initialization are
        // cleared automatically so we can simply exit.
        logErr("Unable to register event callbacks");
        return SCS_RESULT_generic_error;
    }

    // Register for the configuration info. As this example only prints the retrieved
    // data, it can operate even if that fails.

    // Register for channels. The channel might be missing if the game does not support
    // it (SCS_RESULT_not_found) or if does not support the requested type
    // (SCS_RESULT_unsupported_type).
    // !For purpose of this example we ignore the failures
    // !so the unsupported channels will remain at theirs default value.

    scs_result_t retstat;
#define SCS_EtoS retstat == SCS_RESULT_already_registered ? "already registered" : \
    retstat == SCS_RESULT_invalid_parameter ? "invalid parameter" : \
    retstat == SCS_RESULT_not_found ? "not found" : \
    retstat == SCS_RESULT_not_now ? "not now" : \
    retstat == SCS_RESULT_unsupported ? "unsupported" : \
    retstat == SCS_RESULT_unsupported_type ? "unsupported type" : \
    retstat == SCS_RESULT_generic_error ? "generic error" : "unknown error"

#define HANDLE_NOK(what) if(retstat != SCS_RESULT_ok) { \
        logErr("Unable to register function to fetch " what ": %s", SCS_EtoS); \
        return SCS_RESULT_generic_error; \
    }

#define REGISTER_TELEMETRY(channel, type, tdMember, desc) \
    retstat = version_params->register_for_channel( \
        SCS_TELEMETRY_TRUCK_CHANNEL_ ## channel, SCS_U32_NIL, \
        SCS_VALUE_TYPE_ ## type, SCS_TELEMETRY_CHANNEL_FLAG_none, \
        update_ ## type ## _value, &truck_data. ## tdMember); \
    HANDLE_NOK(desc);

    REGISTER_TELEMETRY(electric_enabled, bool, electricity, "truck electricity switched on");
    REGISTER_TELEMETRY(fuel, float, fuel, "current fuel quantity in liters");

    LoadController();
    InitTruckData();
    StartPolling();

    log("G29LedPlugin: Initialization complete");

    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown() {
    log("G29LedPlugin: Shutting down.");
    game_log = nullptr;

    StopPolling();
    UnloadController();
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}