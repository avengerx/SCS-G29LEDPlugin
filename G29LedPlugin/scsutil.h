#ifndef __SCSUTIL_H_INCLUDED__
#define __SCSUTIL_H_INCLUDED__
#include "pch.h"
#include "log.h"

const scs_named_value_t* find_attribute(const scs_telemetry_configuration_t&, const char* const, const scs_u32_t, const scs_value_type_t);

SCSAPI_VOID update_bool_value(const scs_string_t, const scs_u32_t, const scs_value_t* const, const scs_context_t);
SCSAPI_VOID update_float_value(const scs_string_t, const scs_u32_t, const scs_value_t* const, const scs_context_t);
SCSAPI_VOID update_int_value(const scs_string_t, const scs_u32_t, const scs_value_t* const, const scs_context_t);
const auto update_s32_value = update_int_value;

#endif