#include "pch.h"
#include "scsutil.h"

/**
 * @brief Finds attribute with specified name in the configuration structure.
 *
 * Returns NULL if the attribute was not found or if it is not of the expected type.
 */
const scs_named_value_t* find_attribute(const scs_telemetry_configuration_t& configuration, const char* const name, const scs_u32_t index, const scs_value_type_t expected_type)
{
    for (const scs_named_value_t* current = configuration.attributes; current->name; ++current) {
        if ((current->index != index) || (strcmp(current->name, name) != 0)) {
            continue;
        }
        if (current->value.type == expected_type) {
            return current;
        }
        logWarn("Attribute %s has unexpected type %u", name, static_cast<unsigned>(current->value.type));
        break;
    }
    return NULL;
}

#define REGFAIL(x, y) logErr("Received " x " while trying to update " y " telemetry value"); \
    return;

#define REGCHECKS(what, valtype) if (value == nullptr) { \
        REGFAIL("null value", what) \
    } else if (value->type != valtype) { \
        REGFAIL("non-matching value type", what) \
    } else if (context == nullptr) { \
        REGFAIL("null context handle", what) \
    }

SCSAPI_VOID update_bool_value(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    REGCHECKS("boolean", SCS_VALUE_TYPE_bool)
        * static_cast<bool*>(context) = value->value_bool.value;
}

SCSAPI_VOID update_float_value(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    REGCHECKS("floating point", SCS_VALUE_TYPE_float)
        * static_cast<float*>(context) = value->value_float.value;
}

SCSAPI_VOID update_int_value(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    REGCHECKS("integer", SCS_VALUE_TYPE_s32)
        * static_cast<int*>(context) = value->value_s32.value;
}