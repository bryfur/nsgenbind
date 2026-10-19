/* QuickJS binding generation: attribute accessors without a binding body
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Strings, numbers and booleans map to libdom's dom_<class>_get_<name> and
 * dom_<class>_set_<name>; on<event> handlers to the binding user's
 * qjsky_get_event_handler and qjsky_set_event_handler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "options.h"
#include "utils.h"
#include "nsgenbind-ast.h"
#include "webidl-ast.h"
#include "ir.h"
#include "output.h"
#include "qjs-libdom.h"

static bool is_event_handler(struct ir_attribute_entry *atributee)
{
        const char *type = atributee->typev[0].name;
        size_t len;

        if (type == NULL) {
                return false;
        }
        len = strlen(type);
        return (len >= 12) && (strcmp(type + len - 12, "EventHandler") == 0) &&
               (atributee->name[0] == 'o') && (atributee->name[1] == 'n');
}

static void output_event_target(struct opctx *outc,
                                struct ir_entry *interfacee)
{
        if (interfacee->u.interface.primary_global) {
                outputf(outc, "\tdom_event_target *et = NULL;\n\t(void)priv;\n");
        } else {
                outputf(outc,
                        "\tdom_event_target *et = (dom_event_target *)(((node_private_t *)priv)->node);\n");
        }
}

static void output_libdom_target(struct opctx *outc,
                                 struct ir_entry *interfacee)
{
        outputf(outc, "(struct dom_%s *)((node_private_t *)priv)->node",
                interfacee->class_name);
}

/* exported function documented in qjs-libdom.h */
int qjs_output_generated_attribute_getter(struct opctx *outc,
                                          struct ir_entry *interfacee,
                                          struct ir_attribute_entry *atributee)
{
        struct ir_type_entry *type;

        if (atributee->typec != 1) {
                return -1;
        }
        type = atributee->typev;

        switch (type->base) {
        case WEBIDL_TYPE_STRING:
                outputf(outc,
                        "\tdom_exception exc;\n"
                        "\tdom_string *str;\n"
                        "\tJSValue ret;\n\n"
                        "\texc = dom_%s_get_%s(",
                        interfacee->class_name, atributee->property_name);
                output_libdom_target(outc, interfacee);
                outputf(outc,
                        ", &str);\n"
                        "\tif (exc != DOM_NO_ERR) {\n"
                        "\t\treturn JS_UNDEFINED;\n"
                        "\t}\n"
                        "\tif (str == NULL) {\n"
                        "\t\treturn %s;\n"
                        "\t}\n"
                        "\tret = JS_NewStringLen(ctx, dom_string_data(str), dom_string_length(str));\n"
                        "\tdom_string_unref(str);\n"
                        "\treturn ret;\n",
                        type->nullable ? "JS_NULL" : "JS_NewStringLen(ctx, \"\", 0)");
                break;

        case WEBIDL_TYPE_LONG:
        case WEBIDL_TYPE_SHORT: {
                bool is_unsigned = (type->modifier == WEBIDL_TYPE_MODIFIER_UNSIGNED);
                bool is_long = (type->base == WEBIDL_TYPE_LONG);

                outputf(outc,
                        "\tdom_exception exc;\n"
                        "\tdom_%s%s v;\n\n"
                        "\texc = dom_%s_get_%s(",
                        is_unsigned ? "u" : "",
                        is_long ? "long" : "short",
                        interfacee->class_name, atributee->property_name);
                output_libdom_target(outc, interfacee);
                outputf(outc,
                        ", &v);\n"
                        "\tif (exc != DOM_NO_ERR) {\n"
                        "\t\treturn JS_UNDEFINED;\n"
                        "\t}\n"
                        "\treturn %s(ctx, v);\n",
                        is_unsigned ? "JS_NewUint32" : "JS_NewInt32");
                break;
        }

        case WEBIDL_TYPE_BOOL:
                outputf(outc,
                        "\tdom_exception exc;\n"
                        "\tbool b;\n\n"
                        "\texc = dom_%s_get_%s(",
                        interfacee->class_name, atributee->property_name);
                output_libdom_target(outc, interfacee);
                outputf(outc,
                        ", &b);\n"
                        "\tif (exc != DOM_NO_ERR) {\n"
                        "\t\treturn JS_UNDEFINED;\n"
                        "\t}\n"
                        "\treturn JS_NewBool(ctx, b);\n");
                break;

        case WEBIDL_TYPE_USER:
                if (!is_event_handler(atributee)) {
                        return -1;
                }
                output_event_target(outc, interfacee);
                outputf(outc,
                        "\treturn %s_get_event_handler(ctx, this_val, corestring_dom_%s, et);\n",
                        QLPFX, atributee->name + 2);
                break;

        default:
                return -1;
        }

        WARN(WARNING_GENERATED, "Generated: getter %s::%s();",
             interfacee->name, atributee->name);

        return 0;
}

/* exported function documented in qjs-libdom.h */
int qjs_output_generated_attribute_setter(struct opctx *outc,
                                          struct ir_entry *interfacee,
                                          struct ir_attribute_entry *atributee)
{
        struct ir_type_entry *type;

        if (atributee->typec != 1) {
                return -1;
        }
        type = atributee->typev;

        switch (type->base) {
        case WEBIDL_TYPE_STRING:
                outputf(outc,
                        "\tdom_exception exc;\n"
                        "\tdom_string *str;\n"
                        "\tconst char *s;\n"
                        "\tsize_t slen;\n\n");
                if ((atributee->treatnullas != NULL) &&
                    (strcmp(atributee->treatnullas, "EmptyString") == 0)) {
                        outputf(outc,
                                "\tif (JS_IsNull(val)) {\n"
                                "\t\texc = dom_string_create((const uint8_t *)\"\", 0, &str);\n"
                                "\t} else {\n"
                                "\t\ts = JS_ToCStringLen(ctx, &slen, val);\n"
                                "\t\tif (s == NULL) {\n"
                                "\t\t\treturn JS_EXCEPTION;\n"
                                "\t\t}\n"
                                "\t\texc = dom_string_create((const uint8_t *)s, slen, &str);\n"
                                "\t\tJS_FreeCString(ctx, s);\n"
                                "\t}\n");
                } else {
                        outputf(outc,
                                "\ts = JS_ToCStringLen(ctx, &slen, val);\n"
                                "\tif (s == NULL) {\n"
                                "\t\treturn JS_EXCEPTION;\n"
                                "\t}\n"
                                "\texc = dom_string_create((const uint8_t *)s, slen, &str);\n"
                                "\tJS_FreeCString(ctx, s);\n");
                }
                outputf(outc,
                        "\tif (exc != DOM_NO_ERR) {\n"
                        "\t\treturn JS_ThrowOutOfMemory(ctx);\n"
                        "\t}\n"
                        "\texc = dom_%s_set_%s(",
                        interfacee->class_name, atributee->property_name);
                output_libdom_target(outc, interfacee);
                outputf(outc,
                        ", str);\n"
                        "\tdom_string_unref(str);\n"
                        "\t(void)exc;\n"
                        "\treturn JS_UNDEFINED;\n");
                break;

        case WEBIDL_TYPE_LONG:
        case WEBIDL_TYPE_SHORT: {
                bool is_unsigned = (type->modifier == WEBIDL_TYPE_MODIFIER_UNSIGNED);
                bool is_long = (type->base == WEBIDL_TYPE_LONG);

                outputf(outc,
                        "\tint32_t v;\n\n"
                        "\tif (JS_ToInt32(ctx, &v, val) < 0) {\n"
                        "\t\treturn JS_EXCEPTION;\n"
                        "\t}\n"
                        "\tdom_%s_set_%s(",
                        interfacee->class_name, atributee->property_name);
                output_libdom_target(outc, interfacee);
                outputf(outc, ", (dom_%s%s)v);\n\treturn JS_UNDEFINED;\n",
                        is_unsigned ? "u" : "",
                        is_long ? "long" : "short");
                break;
        }

        case WEBIDL_TYPE_BOOL:
                outputf(outc,
                        "\tint b = JS_ToBool(ctx, val);\n\n"
                        "\tif (b < 0) {\n"
                        "\t\treturn JS_EXCEPTION;\n"
                        "\t}\n"
                        "\tdom_%s_set_%s(",
                        interfacee->class_name, atributee->property_name);
                output_libdom_target(outc, interfacee);
                outputf(outc, ", b);\n\treturn JS_UNDEFINED;\n");
                break;

        case WEBIDL_TYPE_USER:
                if (!is_event_handler(atributee)) {
                        return -1;
                }
                output_event_target(outc, interfacee);
                outputf(outc,
                        "\treturn %s_set_event_handler(ctx, this_val, corestring_dom_%s, val, et);\n",
                        QLPFX, atributee->name + 2);
                break;

        default:
                return -1;
        }

        WARN(WARNING_GENERATED, "Generated: setter %s::%s();",
             interfacee->name, atributee->name);

        return 0;
}
