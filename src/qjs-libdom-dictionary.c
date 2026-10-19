/* QuickJS binding generation: dictionaries
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * A member accessor per dictionary member:
 *   int qjsky_<dictionary>_get_<member>(JSContext *ctx, JSValueConst dict, T *out)
 * the member's value (converted as WebIDL says) or its default in *out; 0,
 * or -1 with an exception pending.  Strings come as JSValues (the caller
 * frees them).
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

static enum webidl_type *member_type(struct ir_entry *dictionarye,
                                     struct ir_operation_argument_entry *membere)
{
        struct webidl_node *type_node;
        enum webidl_type *type;

        type_node = webidl_node_find_type(webidl_node_getnode(membere->node),
                                          NULL, WEBIDL_NODE_TYPE_TYPE);
        if (type_node == NULL) {
                fprintf(stderr, "%s:%s has no type\n",
                        dictionarye->name, membere->name);
                return NULL;
        }
        type = (enum webidl_type *)webidl_node_getint(
                webidl_node_find_type(webidl_node_getnode(type_node),
                                      NULL, WEBIDL_NODE_TYPE_TYPE_BASE));
        if (type == NULL) {
                fprintf(stderr, "%s:%s has no type base\n",
                        dictionarye->name, membere->name);
        }
        return type;
}

/**
 * the C type of a member's out parameter, NULL if not handled
 */
static const char *member_ctype(enum webidl_type type)
{
        switch (type) {
        case WEBIDL_TYPE_STRING:
                return "JSValue";
        case WEBIDL_TYPE_BOOL:
                return "bool";
        case WEBIDL_TYPE_SHORT:
        case WEBIDL_TYPE_LONG:
        case WEBIDL_TYPE_LONGLONG:
                return "int32_t";
        case WEBIDL_TYPE_FLOAT:
        case WEBIDL_TYPE_DOUBLE:
                return "double";
        default:
                return NULL;
        }
}

/**
 * the default as a C expression of the member's type
 */
static char *member_default(struct ir_operation_argument_entry *membere,
                            enum webidl_type type)
{
        struct webidl_node *lit_node;
        char *defl = malloc(256);

        lit_node = webidl_node_getnode(
                webidl_node_find_type(webidl_node_getnode(membere->node),
                                      NULL, WEBIDL_NODE_TYPE_OPTIONAL));

        if (type == WEBIDL_TYPE_STRING) {
                if ((lit_node != NULL) &&
                    (webidl_node_gettype(lit_node) == WEBIDL_NODE_TYPE_LITERAL_STRING)) {
                        snprintf(defl, 256, "JS_NewString(ctx, \"%s\")",
                                 webidl_node_gettext(lit_node));
                } else if ((lit_node != NULL) &&
                           (webidl_node_gettype(lit_node) == WEBIDL_NODE_TYPE_LITERAL_NULL)) {
                        snprintf(defl, 256, "JS_NULL");
                } else {
                        snprintf(defl, 256, "JS_UNDEFINED");
                }
                return defl;
        }

        snprintf(defl, 256, "0");
        if (lit_node == NULL) {
                return defl;
        }
        switch (webidl_node_gettype(lit_node)) {
        case WEBIDL_NODE_TYPE_LITERAL_BOOL:
                snprintf(defl, 256, "%s",
                         *webidl_node_getint(lit_node) ? "true" : "false");
                break;
        case WEBIDL_NODE_TYPE_LITERAL_INT:
                snprintf(defl, 256, "%d", *webidl_node_getint(lit_node));
                break;
        case WEBIDL_NODE_TYPE_LITERAL_FLOAT:
                snprintf(defl, 256, "%f", *webidl_node_getfloat(lit_node));
                break;
        default:
                break;
        }
        return defl;
}

static int output_member_accessor(struct opctx *outc,
                                  struct ir_entry *dictionarye,
                                  struct ir_operation_argument_entry *membere)
{
        enum webidl_type *type = member_type(dictionarye, membere);
        const char *ctype;
        char *defl;

        if (type == NULL) {
                return -1;
        }
        ctype = member_ctype(*type);
        if (ctype == NULL) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Dictionary %s:%s unhandled type (%d)",
                     dictionarye->name, membere->name, *type);
                outputf(outc, "/* Dictionary %s:%s unhandled type (%d) */\n\n",
                        dictionarye->name, membere->name, *type);
                return 0;
        }

        defl = member_default(membere, *type);
        outputf(outc,
                "int %s_%s_get_%s(JSContext *ctx, JSValueConst dict, %s *out)\n"
                "{\n"
                "\tJSValue v = JS_UNDEFINED;\n"
                "\tint r = 0;\n\n"
                "\tif (JS_IsObject(dict)) {\n"
                "\t\tv = JS_GetPropertyStr(ctx, dict, \"%s\");\n"
                "\t\tif (JS_IsException(v)) {\n"
                "\t\t\treturn -1;\n"
                "\t\t}\n"
                "\t}\n"
                "\tif (JS_IsUndefined(v)) {\n"
                "\t\t*out = %s;\n"
                "\t\treturn 0;\n"
                "\t}\n",
                QLPFX, dictionarye->class_name, membere->name, ctype,
                membere->name, defl);
        free(defl);

        switch (*type) {
        case WEBIDL_TYPE_STRING:
                outputf(outc,
                        "\t*out = JS_ToString(ctx, v);\n"
                        "\tr = JS_IsException(*out) ? -1 : 0;\n");
                break;
        case WEBIDL_TYPE_BOOL:
                outputf(outc,
                        "\tr = JS_ToBool(ctx, v);\n"
                        "\t*out = r > 0;\n"
                        "\tr = r < 0 ? -1 : 0;\n");
                break;
        case WEBIDL_TYPE_FLOAT:
        case WEBIDL_TYPE_DOUBLE:
                outputf(outc, "\tr = JS_ToFloat64(ctx, out, v);\n");
                break;
        default:
                outputf(outc, "\tr = JS_ToInt32(ctx, out, v);\n");
                break;
        }
        outputf(outc,
                "\tJS_FreeValue(ctx, v);\n"
                "\treturn r;\n"
                "}\n\n");

        return 0;
}

/* exported function documented in qjs-libdom.h */
int qjs_output_dictionary(struct ir *ir, struct ir_entry *dictionarye)
{
        struct opctx *dyop;
        int res;
        int memberc;

        res = output_open(dictionarye->filename, &dyop);
        if (res != 0) {
                return res;
        }

        output_tool_preface(dyop);
        output_method_cdata(dyop, ir->binding_node, GENBIND_METHOD_TYPE_PREFACE);
        output_method_cdata(dyop, dictionarye->class, GENBIND_METHOD_TYPE_PREFACE);
        output_tool_prologue(dyop);
        output_method_cdata(dyop, ir->binding_node, GENBIND_METHOD_TYPE_PROLOGUE);
        output_method_cdata(dyop, dictionarye->class, GENBIND_METHOD_TYPE_PROLOGUE);
        outputf(dyop, "\n");

        for (memberc = 0; memberc < dictionarye->u.dictionary.memberc; memberc++) {
                res = output_member_accessor(dyop, dictionarye,
                        dictionarye->u.dictionary.memberv + memberc);
                if (res != 0) {
                        break;
                }
        }

        output_method_cdata(dyop, dictionarye->class, GENBIND_METHOD_TYPE_EPILOGUE);
        output_method_cdata(dyop, ir->binding_node, GENBIND_METHOD_TYPE_EPILOGUE);
        output_method_cdata(dyop, dictionarye->class, GENBIND_METHOD_TYPE_POSTFACE);
        output_method_cdata(dyop, ir->binding_node, GENBIND_METHOD_TYPE_POSTFACE);
        output_close(dyop);

        return res;
}

/* exported function documented in qjs-libdom.h */
int qjs_output_dictionary_declaration(struct opctx *outc,
                                      struct ir_entry *dictionarye)
{
        int memberc;

        for (memberc = 0; memberc < dictionarye->u.dictionary.memberc; memberc++) {
                struct ir_operation_argument_entry *membere;
                enum webidl_type *type;
                const char *ctype;

                membere = dictionarye->u.dictionary.memberv + memberc;
                type = member_type(dictionarye, membere);
                if (type == NULL) {
                        return -1;
                }
                ctype = member_ctype(*type);
                if (ctype == NULL) {
                        continue;
                }
                outputf(outc,
                        "int %s_%s_get_%s(JSContext *ctx, JSValueConst dict, %s *out);\n",
                        QLPFX, dictionarye->class_name, membere->name, ctype);
        }

        return 0;
}
