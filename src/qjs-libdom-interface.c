/* QuickJS binding generation: interfaces
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Per interface: the initialisor, finaliser, creator, the operations (a
 * wrapper checking the arguments as WebIDL says and supplying defaults, then
 * the binding's body), the attribute accessors, the prototype's function
 * list and the class descriptor.
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

/**
 * Compare two nodes to check their c types match.
 */
static bool compare_ctypes(struct genbind_node *a, struct genbind_node *b)
{
        struct genbind_node *ta;
        struct genbind_node *tb;

        ta = genbind_node_find_type(genbind_node_getnode(a),
                                    NULL, GENBIND_NODE_TYPE_NAME);
        tb = genbind_node_find_type(genbind_node_getnode(b),
                                    NULL, GENBIND_NODE_TYPE_NAME);

        while ((ta != NULL) && (tb != NULL)) {
                if (strcmp(genbind_node_gettext(ta),
                           genbind_node_gettext(tb)) != 0) {
                        return false;
                }
                ta = genbind_node_find_type(genbind_node_getnode(a),
                                            ta, GENBIND_NODE_TYPE_NAME);
                tb = genbind_node_find_type(genbind_node_getnode(b),
                                            tb, GENBIND_NODE_TYPE_NAME);
        }

        return ta == tb;
}

static enum ir_init_argtype guess_argtype_from(struct genbind_node *param_node)
{
        const char *type_cdata = NULL;
        struct genbind_node *typename_node;
        bool unsigned_ = false;
        bool int_ = false;
        bool bool_ = false;

        typename_node = genbind_node_find_type(genbind_node_getnode(param_node),
                                               NULL,
                                               GENBIND_NODE_TYPE_NAME);
        while (typename_node != NULL) {
                type_cdata = genbind_node_gettext(typename_node);
                if (strcmp(type_cdata, "unsigned") == 0) {
                        unsigned_ = true;
                } else if (strcmp(type_cdata, "int") == 0) {
                        int_ = true;
                } else if (strcmp(type_cdata, "bool") == 0) {
                        bool_ = true;
                }
                typename_node = genbind_node_find_type(
                        genbind_node_getnode(param_node),
                        typename_node,
                        GENBIND_NODE_TYPE_NAME);
        }

        if ((type_cdata != NULL) && (type_cdata[0] == '*')) {
                return IR_INIT_ARG_POINTER;
        } else if (unsigned_) {
                return IR_INIT_ARG_UNSIGNED;
        } else if (int_) {
                return IR_INIT_ARG_INT;
        } else if (bool_) {
                return IR_INIT_ARG_BOOL;
        }
        return IR_INIT_ARG_POINTER;
}

static struct genbind_node *init_method(struct ir_entry *interfacee)
{
        return genbind_node_find_method(interfacee->class, NULL,
                                        GENBIND_METHOD_TYPE_INIT);
}

/**
 * count the initialisor's parameters and guess their types (once)
 */
static void compute_init_args(struct ir_entry *interfacee)
{
        struct genbind_node *init_node;
        struct genbind_node *param_node;

        if (interfacee->class_init_argt != NULL ||
            interfacee->class_init_argc < 0) {
                return;
        }

        init_node = init_method(interfacee);
        interfacee->class_init_argc = 0;
        param_node = genbind_node_find_type(genbind_node_getnode(init_node),
                                            NULL, GENBIND_NODE_TYPE_PARAMETER);
        while (param_node != NULL) {
                interfacee->class_init_argc++;
                interfacee->class_init_argt = realloc(
                        interfacee->class_init_argt,
                        interfacee->class_init_argc *
                        sizeof(enum ir_init_argtype));
                interfacee->class_init_argt[interfacee->class_init_argc - 1] =
                        guess_argtype_from(param_node);
                param_node = genbind_node_find_type(
                        genbind_node_getnode(init_node),
                        param_node, GENBIND_NODE_TYPE_PARAMETER);
        }
        if (interfacee->class_init_argc == 0) {
                interfacee->class_init_argc = -1; /* computed: none */
        }
}

static int init_argc(struct ir_entry *interfacee)
{
        compute_init_args(interfacee);
        return interfacee->class_init_argc < 0 ? 0 : interfacee->class_init_argc;
}

/**
 * output the initialisor's parameters, with types (", T a, U b") or without
 */
static void output_init_params(struct opctx *outc,
                               struct ir_entry *interfacee,
                               bool types)
{
        struct genbind_node *init_node = init_method(interfacee);
        struct genbind_node *param_node;

        param_node = genbind_node_find_type(genbind_node_getnode(init_node),
                                            NULL, GENBIND_NODE_TYPE_PARAMETER);
        while (param_node != NULL) {
                outputf(outc, ", ");
                if (types) {
                        output_ctype(outc, param_node, true);
                } else {
                        output_cdata(outc, param_node, GENBIND_NODE_TYPE_IDENT);
                }
                param_node = genbind_node_find_type(
                        genbind_node_getnode(init_node),
                        param_node, GENBIND_NODE_TYPE_PARAMETER);
        }
}

static void output_init_declaration(struct opctx *outc,
                                    struct ir_entry *interfacee)
{
        if (interfacee->refcount == 0) {
                outputf(outc, "static ");
        }
        outputf(outc,
                "void %s_%s___init(JSContext *ctx, JSValueConst obj, %s_private_t *priv",
                QLPFX, interfacee->class_name, interfacee->class_name);
        output_init_params(outc, interfacee, true);
        outputc(outc, ')');
}

/**
 * generate the parent initialisor call, each parameter matched by name
 */
static int output_inherit_init(struct opctx *outc,
                               struct ir_entry *interfacee,
                               struct ir_entry *inherite)
{
        struct genbind_node *init_node;
        struct genbind_node *inh_init_node;
        struct genbind_node *inh_param_node;

        if (inherite == NULL) {
                return 0;
        }

        init_node = init_method(interfacee);
        inh_init_node = init_method(inherite);

        outputf(outc, "\t%s_%s___init(ctx, obj, &priv->parent",
                QLPFX, inherite->class_name);

        inh_param_node = genbind_node_find_type(
                genbind_node_getnode(inh_init_node),
                NULL, GENBIND_NODE_TYPE_PARAMETER);
        while (inh_param_node != NULL) {
                struct genbind_node *param_node;
                char *param_name;

                param_name = genbind_node_gettext(
                        genbind_node_find_type(
                                genbind_node_getnode(inh_param_node),
                                NULL, GENBIND_NODE_TYPE_IDENT));
                param_node = genbind_node_find_type_ident(
                        genbind_node_getnode(init_node),
                        NULL, GENBIND_NODE_TYPE_PARAMETER, param_name);
                if (param_node == NULL) {
                        fprintf(stderr,
                                "class \"%s\" (interface %s) parent class \"%s\" (interface %s) initialisor requires a parameter \"%s\" with compatible identifier\n",
                                interfacee->class_name, interfacee->name,
                                inherite->class_name, inherite->name,
                                param_name);
                        return -1;
                }
                outputf(outc, ", ");
                if (!compare_ctypes(param_node, inh_param_node)) {
                        outputc(outc, '(');
                        output_ctype(outc, inh_param_node, false);
                        outputc(outc, ')');
                }
                output_cdata(outc, param_node, GENBIND_NODE_TYPE_IDENT);

                inh_param_node = genbind_node_find_type(
                        genbind_node_getnode(inh_init_node),
                        inh_param_node, GENBIND_NODE_TYPE_PARAMETER);
        }
        outputf(outc, ");\n");

        return 0;
}

static int output_interface_init(struct opctx *outc,
                                 struct ir_entry *interfacee,
                                 struct ir_entry *inherite)
{
        int res;

        output_init_declaration(outc, interfacee);
        outputf(outc, "\n{\n");
        res = output_inherit_init(outc, interfacee, inherite);
        if (res != 0) {
                return res;
        }
        output_ccode(outc, init_method(interfacee));
        outputf(outc, "}\n\n");

        return 0;
}

static void output_interface_fini(struct opctx *outc,
                                  struct ir_entry *interfacee,
                                  struct ir_entry *inherite)
{
        struct genbind_node *fini_node;

        fini_node = genbind_node_find_method(interfacee->class, NULL,
                                             GENBIND_METHOD_TYPE_FINI);

        if (interfacee->refcount == 0) {
                outputf(outc, "static ");
        }
        outputf(outc,
                "void %s_%s___fini(JSRuntime *rt, %s_private_t *priv)\n{\n",
                QLPFX, interfacee->class_name, interfacee->class_name);
        output_ccode(outc, fini_node);
        if (inherite != NULL) {
                outputf(outc, "\t%s_%s___fini(rt, &priv->parent);\n",
                        QLPFX, inherite->class_name);
        }
        outputf(outc, "}\n\n");
}

/**
 * the destroyer (finaliser chain, free), the creator: an object owning a new
 * private struct, then initialised; the global object's creator initialises
 * the given global
 */
static void output_interface_create(struct opctx *outc,
                                    struct ir_entry *interfacee)
{
        bool global = interfacee->u.interface.primary_global;
        int argc = init_argc(interfacee);

        outputf(outc,
                "%svoid %s_%s___destroy(JSRuntime *rt, void *priv)\n"
                "{\n"
                "\t%s_%s___fini(rt, priv);\n"
                "\tfree(priv);\n"
                "}\n\n",
                global ? "" : "static ",
                QLPFX, interfacee->class_name,
                QLPFX, interfacee->class_name);

        if (global) {
                outputf(outc,
                        "void *%s_%s___create_global(JSContext *ctx, JSValueConst obj",
                        QLPFX, interfacee->class_name);
                output_init_params(outc, interfacee, true);
                outputf(outc,
                        ")\n{\n"
                        "\t%s_private_t *priv = calloc(1, sizeof(*priv));\n"
                        "\tif (priv == NULL) {\n"
                        "\t\treturn NULL;\n"
                        "\t}\n"
                        "\t((%s_object_t *)priv)->klass = &%s_class_%s;\n",
                        interfacee->class_name, QLPFX,
                        QLPFX, interfacee->class_name);
                outputf(outc, "\t%s_%s___init(ctx, obj, priv",
                        QLPFX, interfacee->class_name);
                output_init_params(outc, interfacee, false);
                outputf(outc, ");\n\treturn priv;\n}\n\n");
                return;
        }

        outputf(outc, "JSValue %s_%s___create(JSContext *ctx",
                QLPFX, interfacee->class_name);
        output_init_params(outc, interfacee, true);
        outputf(outc,
                ")\n{\n"
                "\t%s_private_t *priv = calloc(1, sizeof(*priv));\n"
                "\tJSValue obj;\n"
                "\n"
                "\tif (priv == NULL) {\n"
                "\t\treturn JS_ThrowOutOfMemory(ctx);\n"
                "\t}\n"
                "\tobj = %s_new_object(ctx, &%s_class_%s, priv);\n"
                "\tif (JS_IsException(obj)) {\n"
                "\t\treturn obj;\n"
                "\t}\n",
                interfacee->class_name,
                QLPFX, QLPFX, interfacee->class_name);
        outputf(outc, "\t%s_%s___init(ctx, obj, priv",
                QLPFX, interfacee->class_name);
        output_init_params(outc, interfacee, false);
        outputf(outc, ");\n\treturn obj;\n}\n\n");

        /* objects made by the binding's user from one pointer (nodes,
         * events, collections) go through the class descriptor
         */
        if ((argc == 1) &&
            (interfacee->class_init_argt[0] == IR_INIT_ARG_POINTER)) {
                outputf(outc,
                        "static JSValue %s_%s___create_ptr(JSContext *ctx, void *ptr)\n"
                        "{\n"
                        "\treturn %s_%s___create(ctx, ptr);\n"
                        "}\n\n",
                        QLPFX, interfacee->class_name,
                        QLPFX, interfacee->class_name);
        }
}

/**
 * generate the statement getting a private struct for a body
 */
static void output_get_private(struct opctx *outc,
                               struct ir_entry *interfacee)
{
        if (interfacee->u.interface.primary_global) {
                outputf(outc,
                        "\t%s_private_t *priv = %s_global_private(ctx);\n",
                        interfacee->class_name, QLPFX);
        } else {
                outputf(outc,
                        "\t%s_private_t *priv = %s_this_private(ctx, this_val, &%s_class_%s);\n",
                        interfacee->class_name, QLPFX,
                        QLPFX, interfacee->class_name);
        }
        outputf(outc,
                "\tif (priv == NULL) {\n"
                "\t\treturn JS_EXCEPTION;\n"
                "\t}\n");
}

static struct webidl_node *argument_type_node(
        struct ir_operation_argument_entry *argumente)
{
        return webidl_node_find_type(webidl_node_getnode(argumente->node),
                                     NULL, WEBIDL_NODE_TYPE_TYPE);
}

/**
 * the interface an argument must implement (an object of another generated
 * interface), NULL if the argument is anything else
 */
static struct ir_entry *argument_interface(struct ir *ir,
        struct ir_operation_argument_entry *argumente,
        bool *nullable)
{
        struct webidl_node *type_node = argument_type_node(argumente);
        enum webidl_type *base;
        struct ir_entry *typee;

        if (type_node == NULL) {
                return NULL;
        }
        base = (enum webidl_type *)webidl_node_getint(
                webidl_node_find_type(webidl_node_getnode(type_node),
                                      NULL, WEBIDL_NODE_TYPE_TYPE_BASE));
        if ((base == NULL) || (*base != WEBIDL_TYPE_USER)) {
                return NULL;
        }
        *nullable = (webidl_node_find_type(webidl_node_getnode(type_node),
                        NULL, WEBIDL_NODE_TYPE_TYPE_NULLABLE) != NULL) ||
                (argumente->optionalc != 0);
        typee = qjs_find_interface(ir,
                webidl_node_gettext(webidl_node_find_type(
                        webidl_node_getnode(type_node),
                        NULL, WEBIDL_NODE_TYPE_IDENT)));
        /* only interfaces the binding implements (a callback interface,
         * EventListener, is any function or object)
         */
        if ((typee == NULL) || (typee->class == NULL)) {
                return NULL;
        }
        return typee;
}

/**
 * generate the default of an optional argument; true if the value must be
 * freed after the body
 */
static bool output_argument_default(struct opctx *outc,
        struct ir_operation_argument_entry *argumente,
        int argidx)
{
        struct webidl_node *lit_node;

        lit_node = webidl_node_getnode(
                webidl_node_find_type(webidl_node_getnode(argumente->node),
                                      NULL, WEBIDL_NODE_TYPE_OPTIONAL));

        outputf(outc, "\t\targs[%d] = ", argidx);
        if (lit_node == NULL) {
                outputf(outc, "JS_UNDEFINED;\n");
                return false;
        }

        switch (webidl_node_gettype(lit_node)) {
        case WEBIDL_NODE_TYPE_LITERAL_NULL:
                outputf(outc, "JS_NULL;\n");
                break;

        case WEBIDL_NODE_TYPE_LITERAL_INT:
                outputf(outc, "JS_NewInt32(ctx, %d);\n",
                        *webidl_node_getint(lit_node));
                break;

        case WEBIDL_NODE_TYPE_LITERAL_BOOL:
                outputf(outc, "JS_NewBool(ctx, %d);\n",
                        *webidl_node_getint(lit_node));
                break;

        case WEBIDL_NODE_TYPE_LITERAL_STRING:
                outputf(outc, "JS_NewString(ctx, \"%s\");\n",
                        webidl_node_gettext(lit_node));
                return true;

        default:
                outputf(outc, "JS_UNDEFINED;\n");
                break;
        }
        return false;
}

/* whether a binding node has code */
static bool has_code(struct genbind_node *node)
{
        return (node != NULL) &&
               (genbind_node_find_type(genbind_node_getnode(node), NULL,
                                       GENBIND_NODE_TYPE_CDATA) != NULL);
}

static bool is_event_handler_attribute(struct ir_attribute_entry *atributee)
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

/* whether an attribute's accessor can be generated (qjs-libdom-generated.c) */
static bool attribute_generable(struct ir_attribute_entry *atributee)
{
        if (atributee->typec != 1) {
                return false;
        }
        switch (atributee->typev[0].base) {
        case WEBIDL_TYPE_STRING:
        case WEBIDL_TYPE_LONG:
        case WEBIDL_TYPE_SHORT:
        case WEBIDL_TYPE_BOOL:
                return true;
        case WEBIDL_TYPE_USER:
                return is_event_handler_attribute(atributee);
        default:
                return false;
        }
}

/* whether an operation has a body (an unimplemented one is not on the
 * prototype at all: scripts feature-detecting see it missing)
 */
static bool operation_implemented(struct ir_operation_entry *operatione)
{
        return (operatione->name != NULL) && has_code(operatione->method);
}

static bool getter_implemented(struct ir_attribute_entry *atributee)
{
        return (atributee->getter != NULL) &&
               (has_code(atributee->getter) || attribute_generable(atributee));
}

static bool setter_implemented(struct ir_attribute_entry *atributee)
{
        if ((atributee->modifier == WEBIDL_TYPE_MODIFIER_READONLY) &&
            (atributee->putforwards == NULL)) {
                return false;
        }
        if (atributee->setter != NULL) {
                return has_code(atributee->setter) || attribute_generable(atributee);
        }
        return (atributee->putforwards != NULL) && getter_implemented(atributee);
}

/**
 * the body of an operation in a function of its own:
 * ctx, this_val, priv, argc, argv in scope
 */
static int output_operation_body(struct opctx *outc,
                                 struct ir_entry *interfacee,
                                 struct ir_operation_entry *operatione)
{
        outputf(outc,
                "static JSValue %s_%s_%s__body(JSContext *ctx, JSValueConst this_val, %s_private_t *priv, int argc, JSValueConst *argv)\n"
                "{\n",
                QLPFX, interfacee->class_name, operatione->name,
                interfacee->class_name);
        if (output_ccode(outc, operatione->method) == 0) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Unimplemented: method %s::%s();",
                     interfacee->name, operatione->name);
                outputf(outc, "\treturn JS_UNDEFINED;\n");
        }
        outputf(outc, "}\n\n");
        return 0;
}

/**
 * the number of arguments an operation requires (its JS length)
 */
static int operation_length(struct ir_operation_entry *operatione)
{
        int length = -1;
        int i;

        for (i = 0; i < operatione->overloadc; i++) {
                struct ir_operation_overload_entry *o = operatione->overloadv + i;
                int fixed = o->argumentc - o->optionalc - o->elipsisc;
                if ((length < 0) || (fixed < length)) {
                        length = fixed;
                }
        }
        return length < 0 ? 0 : length;
}

static int output_interface_operation(struct opctx *outc,
                                      struct ir *ir,
                                      struct ir_entry *interfacee,
                                      struct ir_operation_entry *operatione)
{
        struct ir_operation_overload_entry *overloade;
        int fixedargc;
        int argidx;
        bool simple;
        bool *freed = NULL;

        if (operatione->name == NULL) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Special operation on interface %s (operation entry %p)",
                     interfacee->name, operatione);
                outputf(outc, "/* Special method definition - UNIMPLEMENTED */\n\n");
                return 0;
        }

        if (!operation_implemented(operatione)) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Unimplemented: method %s::%s();",
                     interfacee->name, operatione->name);
                return 0;
        }

        output_operation_body(outc, interfacee, operatione);

        /* overloads and variadic operations are checked by their bodies */
        overloade = operatione->overloadv;
        simple = (operatione->overloadc == 1) && (overloade->elipsisc == 0);
        if (!simple) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Overloaded or variadic parameters not checked: method %s::%s();",
                     interfacee->name, operatione->name);
        }

        outputf(outc,
                "static JSValue %s_%s_%s(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)\n"
                "{\n",
                QLPFX, interfacee->class_name, operatione->name);

        if (!simple || (overloade->argumentc == 0)) {
                output_get_private(outc, interfacee);
                outputf(outc,
                        "\treturn %s_%s_%s__body(ctx, this_val, priv, argc, argv);\n"
                        "}\n\n",
                        QLPFX, interfacee->class_name, operatione->name);
                return 0;
        }

        fixedargc = overloade->argumentc - overloade->optionalc;

        if (overloade->optionalc > 0) {
                outputf(outc, "\tJSValueConst args[%d];\n", overloade->argumentc);
                outputf(outc, "\tJSValue ret;\n");
        }
        if (fixedargc > 0) {
                outputf(outc,
                        "\n\tif (argc < %d) {\n"
                        "\t\treturn JS_ThrowTypeError(ctx, \"%s.%s: %d argument%s required, but only %%d present\", argc);\n"
                        "\t}\n",
                        fixedargc,
                        interfacee->name, operatione->name,
                        fixedargc, fixedargc == 1 ? "" : "s");
        }
        output_get_private(outc, interfacee);

        /* arguments which must be objects of an interface */
        for (argidx = 0; argidx < overloade->argumentc; argidx++) {
                struct ir_operation_argument_entry *argumente;
                struct ir_entry *typee;
                bool nullable = false;

                argumente = overloade->argumentv + argidx;
                typee = argument_interface(ir, argumente, &nullable);
                if (typee == NULL) {
                        continue;
                }
                outputf(outc, "\tif ((argc > %d) && ", argidx);
                if (nullable) {
                        outputf(outc,
                                "!JS_IsNull(argv[%1$d]) && !JS_IsUndefined(argv[%1$d]) && ",
                                argidx);
                }
                outputf(outc,
                        "!%s_instanceof(argv[%d], &%s_class_%s)) {\n"
                        "\t\treturn JS_ThrowTypeError(ctx, \"%s.%s: argument %d is not a%s %s\");\n"
                        "\t}\n",
                        QLPFX, argidx, QLPFX, typee->class_name,
                        interfacee->name, operatione->name, argidx + 1,
                        strchr("AEIOU", typee->name[0]) ? "n" : "",
                        typee->name);
        }

        if (overloade->optionalc == 0) {
                outputf(outc,
                        "\treturn %s_%s_%s__body(ctx, this_val, priv, argc, argv);\n"
                        "}\n\n",
                        QLPFX, interfacee->class_name, operatione->name);
                return 0;
        }

        /* the missing optional arguments' defaults */
        freed = calloc(overloade->argumentc, sizeof(bool));
        outputf(outc,
                "\tif (argc < %d) {\n"
                "\t\tint i;\n"
                "\t\tfor (i = 0; i < argc; i++) {\n"
                "\t\t\targs[i] = argv[i];\n"
                "\t\t}\n",
                overloade->argumentc);
        for (argidx = fixedargc; argidx < overloade->argumentc; argidx++) {
                outputf(outc, "\t\tif (argc <= %d)\n\t", argidx);
                freed[argidx] = output_argument_default(outc,
                        overloade->argumentv + argidx, argidx);
        }
        outputf(outc,
                "\t\tret = %s_%s_%s__body(ctx, this_val, priv, %d, args);\n",
                QLPFX, interfacee->class_name, operatione->name,
                overloade->argumentc);
        for (argidx = fixedargc; argidx < overloade->argumentc; argidx++) {
                if (freed[argidx]) {
                        outputf(outc,
                                "\t\tif (argc <= %d) {\n"
                                "\t\t\tJS_FreeValue(ctx, args[%d]);\n"
                                "\t\t}\n",
                                argidx, argidx);
                }
        }
        free(freed);
        outputf(outc,
                "\t\treturn ret;\n"
                "\t}\n"
                "\treturn %s_%s_%s__body(ctx, this_val, priv, argc, argv);\n"
                "}\n\n",
                QLPFX, interfacee->class_name, operatione->name);

        return 0;
}

static void output_attribute_getter(struct opctx *outc,
                                    struct ir_entry *interfacee,
                                    struct ir_attribute_entry *atributee)
{
        int res = -1;

        outputf(outc,
                "static JSValue %s_%s_%s_getter(JSContext *ctx, JSValueConst this_val)\n"
                "{\n",
                QLPFX, interfacee->class_name, atributee->name);
        output_get_private(outc, interfacee);

        if (atributee->getter != NULL) {
                res = output_ccode(outc, atributee->getter);
                if (res == 0) {
                        res = qjs_output_generated_attribute_getter(outc,
                                interfacee, atributee);
                }
        }
        if (res < 0) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Unimplemented: getter %s::%s();",
                     interfacee->name, atributee->name);
                outputf(outc, "\t(void)priv;\n\treturn JS_UNDEFINED;\n");
        }
        outputf(outc, "}\n\n");
}

static void output_attribute_setter(struct opctx *outc,
                                    struct ir_entry *interfacee,
                                    struct ir_attribute_entry *atributee)
{
        int res = -1;

        outputf(outc,
                "static JSValue %s_%s_%s_setter(JSContext *ctx, JSValueConst this_val, JSValueConst val)\n"
                "{\n",
                QLPFX, interfacee->class_name, atributee->name);
        output_get_private(outc, interfacee);

        if (atributee->setter != NULL) {
                res = output_ccode(outc, atributee->setter);
                if (res == 0) {
                        res = qjs_output_generated_attribute_setter(outc,
                                interfacee, atributee);
                }
        } else if (atributee->putforwards != NULL) {
                /* [PutForwards]: set the named property of the value read */
                outputf(outc,
                        "\tJSValue target = %s_%s_%s_getter(ctx, this_val);\n"
                        "\tint r;\n"
                        "\t(void)priv;\n"
                        "\tif (JS_IsException(target)) {\n"
                        "\t\treturn target;\n"
                        "\t}\n"
                        "\tr = JS_SetPropertyStr(ctx, target, \"%s\", JS_DupValue(ctx, val));\n"
                        "\tJS_FreeValue(ctx, target);\n"
                        "\treturn r < 0 ? JS_EXCEPTION : JS_UNDEFINED;\n",
                        QLPFX, interfacee->class_name, atributee->name,
                        atributee->putforwards);
                res = 0;
        }
        if (res < 0) {
                WARN(WARNING_UNIMPLEMENTED,
                     "Unimplemented: setter %s::%s();",
                     interfacee->name, atributee->name);
                outputf(outc, "\t(void)priv;\n\treturn JS_UNDEFINED;\n");
        }
        outputf(outc, "}\n\n");
}

static void output_interface_attributes(struct opctx *outc,
                                        struct ir_entry *interfacee)
{
        int attrc;

        for (attrc = 0; attrc < interfacee->u.interface.attributec; attrc++) {
                struct ir_attribute_entry *atributee;

                atributee = interfacee->u.interface.attributev + attrc;
                if (atributee->property_name == NULL) {
                        atributee->property_name =
                                gen_idl2c_name(atributee->name);
                }
                if (!getter_implemented(atributee)) {
                        WARN(WARNING_UNIMPLEMENTED,
                             "Unimplemented: getter %s::%s();",
                             interfacee->name, atributee->name);
                        continue;
                }
                output_attribute_getter(outc, interfacee, atributee);
                if (setter_implemented(atributee)) {
                        output_attribute_setter(outc, interfacee, atributee);
                }
        }
}

static int constant_value(struct ir_constant_entry *constante)
{
        int *value = webidl_node_getint(
                webidl_node_find_type(webidl_node_getnode(constante->node),
                                      NULL, WEBIDL_NODE_TYPE_LITERAL_INT));
        return value == NULL ? 0 : *value;
}

/**
 * a list's hooks: an item(index) operation with a length attribute (the
 * indexed getter), a one argument namedItem or getNamedItem (the named
 * getter)
 */
static void output_list_hooks(struct opctx *outc, struct ir_entry *interfacee)
{
        struct ir_interface_entry *ife = &interfacee->u.interface;
        const char *item = NULL;
        const char *named = NULL;
        bool length = false;
        int i;

        for (i = 0; i < ife->operationc; i++) {
                struct ir_operation_entry *o = ife->operationv + i;
                if (!operation_implemented(o) || (o->overloadc != 1) ||
                    (o->overloadv->argumentc != 1)) {
                        continue;
                }
                if (strcmp(o->name, "item") == 0) {
                        item = o->name;
                } else if ((strcmp(o->name, "namedItem") == 0) ||
                           (strcmp(o->name, "getNamedItem") == 0)) {
                        named = o->name;
                }
        }
        for (i = 0; i < ife->attributec; i++) {
                if ((strcmp(ife->attributev[i].name, "length") == 0) &&
                    getter_implemented(ife->attributev + i)) {
                        length = true;
                }
        }
        if ((item != NULL) && length) {
                outputf(outc,
                        "\t.indexed_item = %s_%s_%s,\n"
                        "\t.indexed_length = %s_%s_length_getter,\n",
                        QLPFX, interfacee->class_name, item,
                        QLPFX, interfacee->class_name);
        }
        if (named != NULL) {
                outputf(outc, "\t.named_item = %s_%s_%s,\n",
                        QLPFX, interfacee->class_name, named);
        }
}

/**
 * the prototype's and interface object's function lists, the class
 */
static void output_interface_class(struct opctx *outc,
                                   struct ir_entry *interfacee,
                                   struct ir_entry *inherite)
{
        struct ir_interface_entry *ife = &interfacee->u.interface;
        char *index_name;
        int i;
        int argc = init_argc(interfacee);
        struct genbind_node *proto_node;

        outputf(outc,
                "static const JSCFunctionListEntry %s_%s_proto_funcs[] = {\n",
                QLPFX, interfacee->class_name);
        for (i = 0; i < ife->operationc; i++) {
                struct ir_operation_entry *operatione = ife->operationv + i;
                if (!operation_implemented(operatione)) {
                        continue;
                }
                outputf(outc, "\tQJSKY_METHOD(\"%s\", %d, %s_%s_%s),\n",
                        operatione->name, operation_length(operatione),
                        QLPFX, interfacee->class_name, operatione->name);
        }
        for (i = 0; i < ife->attributec; i++) {
                struct ir_attribute_entry *atributee = ife->attributev + i;
                if (!getter_implemented(atributee)) {
                        continue;
                }
                outputf(outc, "\tQJSKY_ATTRIBUTE(\"%s\", %s_%s_%s_getter, ",
                        atributee->name,
                        QLPFX, interfacee->class_name, atributee->name);
                if (setter_implemented(atributee)) {
                        outputf(outc, "%s_%s_%s_setter),\n",
                                QLPFX, interfacee->class_name,
                                atributee->name);
                } else {
                        outputf(outc, "NULL),\n");
                }
        }
        for (i = 0; i < ife->constantc; i++) {
                outputf(outc, "\tQJSKY_CONSTANT(\"%s\", %d),\n",
                        ife->constantv[i].name,
                        constant_value(ife->constantv + i));
        }
        outputf(outc, "\tQJSKY_TAG(\"%s\"),\n};\n\n", interfacee->name);

        if (ife->constantc > 0) {
                outputf(outc,
                        "static const JSCFunctionListEntry %s_%s_ctor_funcs[] = {\n",
                        QLPFX, interfacee->class_name);
                for (i = 0; i < ife->constantc; i++) {
                        outputf(outc, "\tQJSKY_CONSTANT(\"%s\", %d),\n",
                                ife->constantv[i].name,
                                constant_value(ife->constantv + i));
                }
                outputf(outc, "};\n\n");
        }

        proto_node = genbind_node_find_method(interfacee->class, NULL,
                                              GENBIND_METHOD_TYPE_PROTOTYPE);
        if (proto_node != NULL) {
                outputf(outc,
                        "static int %s_%s___proto_init(JSContext *ctx, JSValueConst proto)\n"
                        "{\n",
                        QLPFX, interfacee->class_name);
                output_ccode(outc, proto_node);
                outputf(outc, "\treturn 0;\n}\n\n");
        }

        index_name = qjs_class_index_name(interfacee);
        outputf(outc,
                "const %s_class_t %s_class_%s = {\n"
                "\t.name = \"%s\",\n",
                QLPFX, QLPFX, interfacee->class_name,
                interfacee->name);
        if (inherite != NULL) {
                outputf(outc, "\t.parent = &%s_class_%s,\n",
                        QLPFX, inherite->class_name);
        }
        outputf(outc,
                "\t.index = %s,\n"
                "\t.proto_funcs = %s_%s_proto_funcs,\n"
                "\t.proto_funcs_count = sizeof(%s_%s_proto_funcs) / sizeof(JSCFunctionListEntry),\n",
                index_name,
                QLPFX, interfacee->class_name,
                QLPFX, interfacee->class_name);
        if (ife->constantc > 0) {
                outputf(outc,
                        "\t.ctor_funcs = %s_%s_ctor_funcs,\n"
                        "\t.ctor_funcs_count = %d,\n",
                        QLPFX, interfacee->class_name, ife->constantc);
        }
        if (proto_node != NULL) {
                outputf(outc, "\t.proto_init = %s_%s___proto_init,\n",
                        QLPFX, interfacee->class_name);
        }
        outputf(outc, "\t.destroy = %s_%s___destroy,\n",
                QLPFX, interfacee->class_name);
        output_list_hooks(outc, interfacee);
        if (!ife->primary_global && (argc == 1) &&
            (interfacee->class_init_argt[0] == IR_INIT_ARG_POINTER)) {
                outputf(outc, "\t.create = %s_%s___create_ptr,\n",
                        QLPFX, interfacee->class_name);
        }
        outputf(outc, "};\n\n");
        free(index_name);
}

/* exported interface documented in qjs-libdom.h */
int qjs_output_interface(struct ir *ir, struct ir_entry *interfacee)
{
        struct opctx *ifop;
        struct ir_entry *inherite;
        int res;
        int opc;

        res = output_open(interfacee->filename, &ifop);
        if (res != 0) {
                return res;
        }

        inherite = ir_inherit_entry(ir, interfacee);

        output_tool_preface(ifop);
        output_method_cdata(ifop, ir->binding_node, GENBIND_METHOD_TYPE_PREFACE);
        output_method_cdata(ifop, interfacee->class, GENBIND_METHOD_TYPE_PREFACE);
        output_tool_prologue(ifop);
        output_method_cdata(ifop, ir->binding_node, GENBIND_METHOD_TYPE_PROLOGUE);
        output_method_cdata(ifop, interfacee->class, GENBIND_METHOD_TYPE_PROLOGUE);
        outputf(ifop, "\n");

        res = output_interface_init(ifop, interfacee, inherite);
        if (res != 0) {
                goto op_error;
        }
        output_interface_fini(ifop, interfacee, inherite);
        output_interface_create(ifop, interfacee);

        for (opc = 0; opc < interfacee->u.interface.operationc; opc++) {
                res = output_interface_operation(ifop, ir, interfacee,
                        interfacee->u.interface.operationv + opc);
                if (res != 0) {
                        goto op_error;
                }
        }
        output_interface_attributes(ifop, interfacee);
        output_interface_class(ifop, interfacee, inherite);

        output_method_cdata(ifop, interfacee->class, GENBIND_METHOD_TYPE_EPILOGUE);
        output_method_cdata(ifop, ir->binding_node, GENBIND_METHOD_TYPE_EPILOGUE);
        output_method_cdata(ifop, interfacee->class, GENBIND_METHOD_TYPE_POSTFACE);
        output_method_cdata(ifop, ir->binding_node, GENBIND_METHOD_TYPE_POSTFACE);

op_error:
        output_close(ifop);

        return res;
}

/* exported interface documented in qjs-libdom.h */
int qjs_output_interface_declaration(struct opctx *outc,
                                     struct ir_entry *interfacee)
{
        if (interfacee->u.interface.noobject) {
                return 0;
        }

        outputf(outc, "extern const %s_class_t %s_class_%s;\n",
                QLPFX, QLPFX, interfacee->class_name);

        if (interfacee->u.interface.primary_global) {
                outputf(outc,
                        "void *%s_%s___create_global(JSContext *ctx, JSValueConst obj",
                        QLPFX, interfacee->class_name);
                output_init_params(outc, interfacee, true);
                outputf(outc, ");\n");
                outputf(outc,
                        "void %s_%s___destroy(JSRuntime *rt, void *priv);\n",
                        QLPFX, interfacee->class_name);
        } else {
                outputf(outc, "JSValue %s_%s___create(JSContext *ctx",
                        QLPFX, interfacee->class_name);
                output_init_params(outc, interfacee, true);
                outputf(outc, ");\n");
        }

        /* the initialisor and finaliser of interfaces others inherit */
        if (interfacee->refcount > 0) {
                outputf(outc,
                        "void %s_%s___fini(JSRuntime *rt, %s_private_t *priv);\n",
                        QLPFX, interfacee->class_name,
                        interfacee->class_name);
                output_init_declaration(outc, interfacee);
                outputf(outc, ";\n");
        }
        outputf(outc, "\n");

        return 0;
}
