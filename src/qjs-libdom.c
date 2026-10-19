/* QuickJS binding generation implementation
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * The bindings are native QuickJS: one JS class for every DOM object, its
 * opaque the interface's private struct, the interfaces described by
 * generated constant tables (qjsky_class_t), prototypes made per context
 * on first use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "options.h"
#include "utils.h"
#include "nsgenbind-ast.h"
#include "webidl-ast.h"
#include "ir.h"
#include "output.h"
#include "qjs-libdom.h"

/* exported interface documented in qjs-libdom.h */
char *qjs_class_index_name(const struct ir_entry *entry)
{
        char *name;
        int i;
        int len;

        len = strlen(entry->class_name) + SLEN("QJSKY_CLASS_") + 1;
        name = malloc(len);
        snprintf(name, len, "QJSKY_CLASS_%s", entry->class_name);
        for (i = 0; name[i] != 0; i++) {
                name[i] = toupper(name[i]);
        }
        return name;
}

/* exported interface documented in qjs-libdom.h */
struct ir_entry *qjs_find_interface(struct ir *ir, const char *name)
{
        int idx;

        if (name == NULL) {
                return NULL;
        }
        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;
                if ((entry->type == IR_ENTRY_TYPE_INTERFACE) &&
                    (!entry->u.interface.noobject) &&
                    (strcmp(entry->name, name) == 0)) {
                        return entry;
                }
        }
        return NULL;
}

static struct opctx *open_header(struct ir *ir, const char *name)
{
        char fname[64];
        struct opctx *hdrc;

        snprintf(fname, sizeof(fname), "%s.h", name);
        if (output_open(fname, &hdrc) != 0) {
                return NULL;
        }

        output_tool_preface(hdrc);
        output_method_cdata(hdrc, ir->binding_node,
                            GENBIND_METHOD_TYPE_PREFACE);
        outputf(hdrc, "\n#ifndef %s_%s_h\n", QLPFX, name);
        outputf(hdrc, "#define %s_%s_h\n\n", QLPFX, name);

        return hdrc;
}

static int close_header(struct ir *ir, struct opctx *hdrc)
{
        outputf(hdrc, "\n#endif\n");
        output_method_cdata(hdrc, ir->binding_node,
                            GENBIND_METHOD_TYPE_POSTFACE);
        output_close(hdrc);
        return 0;
}

static bool generated(struct ir_entry *entry)
{
        return !((entry->type == IR_ENTRY_TYPE_INTERFACE) &&
                 (entry->u.interface.noobject));
}

/**
 * generate private header: the private structs, each nesting its parent's,
 * the root of each chain beginning with the object header
 */
static int output_private_header(struct ir *ir)
{
        int idx;
        struct opctx *privc;

        privc = open_header(ir, "private");
        if (privc == NULL) {
                return -1;
        }

        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;
                struct ir_entry *inherite;
                struct genbind_node *priv_node;

                if (!generated(entry)) {
                        continue;
                }

                outputf(privc, "/* Private data for %s %s */\n",
                        entry->name,
                        entry->type == IR_ENTRY_TYPE_INTERFACE ?
                        "interface" : "dictionary");
                outputf(privc, "typedef struct {\n");

                inherite = ir_inherit_entry(ir, entry);
                if (inherite != NULL) {
                        outputf(privc, "\t%s_private_t parent;\n",
                                inherite->class_name);
                } else {
                        outputf(privc, "\t%s_object_t object;\n", QLPFX);
                }

                priv_node = genbind_node_find_type(
                        genbind_node_getnode(entry->class),
                        NULL, GENBIND_NODE_TYPE_PRIVATE);
                while (priv_node != NULL) {
                        outputc(privc, '\t');
                        output_ctype(privc, priv_node, true);
                        outputf(privc, ";\n");
                        priv_node = genbind_node_find_type(
                                genbind_node_getnode(entry->class),
                                priv_node, GENBIND_NODE_TYPE_PRIVATE);
                }

                outputf(privc, "} __attribute__((aligned)) %s_private_t;\n\n",
                        entry->class_name);
        }

        return close_header(ir, privc);
}

/**
 * generate prototype header: the class descriptors, creators, initialisors
 * and finalisers
 */
static int output_prototype_header(struct ir *ir)
{
        int idx;
        struct opctx *protoc;

        protoc = open_header(ir, "prototype");
        if (protoc == NULL) {
                return -1;
        }

        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;

                switch (entry->type) {
                case IR_ENTRY_TYPE_INTERFACE:
                        qjs_output_interface_declaration(protoc, entry);
                        break;

                case IR_ENTRY_TYPE_DICTIONARY:
                        qjs_output_dictionary_declaration(protoc, entry);
                        break;
                }
        }

        return close_header(ir, protoc);
}

/**
 * generate makefile fragment
 */
static int output_makefile(struct ir *ir)
{
        int idx;
        FILE *makef;

        makef = genb_fopen_tmp("Makefile");
        if (makef == NULL) {
                return -1;
        }

        fprintf(makef, "# qjs libdom makefile fragment\n\n");
        fprintf(makef, "NSGENBIND_SOURCES:=binding.c ");
        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;
                if (generated(entry)) {
                        fprintf(makef, "%s ", entry->filename);
                }
        }
        fprintf(makef, "\nNSGENBIND_PREFIX:=%s\n", options->outdirname);

        genb_fclose_tmp(makef, "Makefile");

        return 0;
}

/**
 * generate binding header: the object model, the class indexes, the API the
 * generated code and the binding's user share
 */
static int output_binding_header(struct ir *ir)
{
        int idx;
        struct opctx *bindc;

        bindc = open_header(ir, "binding");
        if (bindc == NULL) {
                return -1;
        }

        outputf(bindc,
"#include <stdbool.h>\n"
"#include <quickjs.h>\n"
"\n"
"typedef struct %1$s_class_s %1$s_class_t;\n"
"\n"
"/* the start of every private struct: the object's interface */\n"
"typedef struct %1$s_object_s {\n"
"\tconst %1$s_class_t *klass;\n"
"} %1$s_object_t;\n"
"\n"
"/* an interface (generated, constant) */\n"
"struct %1$s_class_s {\n"
"\tconst char *name;\n"
"\tconst %1$s_class_t *parent;\n"
"\tint index;\n"
"\tconst JSCFunctionListEntry *proto_funcs; /* the prototype's properties */\n"
"\tint proto_funcs_count;\n"
"\tconst JSCFunctionListEntry *ctor_funcs; /* the interface object's (constants) */\n"
"\tint ctor_funcs_count;\n"
"\tint (*proto_init)(JSContext *ctx, JSValueConst proto); /* the binding's prototype hook */\n"
"\tvoid (*destroy)(JSRuntime *rt, void *priv); /* fini chain, free */\n"
"\tJSValue (*create)(JSContext *ctx, void *ptr); /* NULL: init does not take one pointer */\n"
"\t/* a list (WebIDL's indexed and named getters): obj[i] is item(i) below\n"
"\t * length, obj.name is namedItem(name) for names the prototypes lack\n"
"\t */\n"
"\tJSValue (*indexed_item)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);\n"
"\tJSValue (*indexed_length)(JSContext *ctx, JSValueConst this_val);\n"
"\tJSValue (*named_item)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);\n"
"};\n"
"\n"
"/* WebIDL's property attributes for the generated function lists */\n"
"#define %2$s_METHOD(name1, length1, func1) { name1, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE, JS_DEF_CFUNC, 0, { .func = { length1, JS_CFUNC_generic, { .generic = func1 } } } }\n"
"#define %2$s_ATTRIBUTE(name1, fgetter, fsetter) { name1, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE, JS_DEF_CGETSET, 0, { .getset = { .get = { .getter = fgetter }, .set = { .setter = fsetter } } } }\n"
"#define %2$s_CONSTANT(name1, val1) { name1, JS_PROP_ENUMERABLE, JS_DEF_PROP_INT32, 0, { .i32 = val1 } }\n"
"#define %2$s_TAG(name1) { \"[Symbol.toStringTag]\", JS_PROP_CONFIGURABLE, JS_DEF_PROP_STRING, 0, { .str = name1 } }\n"
"\n",
                QLPFX, "QJSKY");

        outputf(bindc, "enum %s_class_index {\n", QLPFX);
        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;
                char *index_name;

                if ((entry->type != IR_ENTRY_TYPE_INTERFACE) ||
                    !generated(entry)) {
                        continue;
                }
                index_name = qjs_class_index_name(entry);
                outputf(bindc, "\t%s,\n", index_name);
                free(index_name);
        }
        outputf(bindc, "\tQJSKY_CLASS_COUNT\n};\n\n");

        outputf(bindc,
"/* every interface, by index */\n"
"extern const %1$s_class_t *const %1$s_classes[QJSKY_CLASS_COUNT];\n"
"\n"
"/* the JS classes of the DOM objects (%1$s_runtime_init): lists have\n"
"%1$s_list_class_id's exotic properties\n"
" */\n"
"extern JSClassID %1$s_class_id;\n"
"extern JSClassID %1$s_list_class_id;\n"
"\n"
"/* the class registered in a runtime: 0, or -1 */\n"
"int %1$s_runtime_init(JSRuntime *rt);\n"
"\n"
"/* the interface objects of every interface defined on a global object\n"
" * (lazily: each is made when first read)\n"
" */\n"
"int %1$s_define_interfaces(JSContext *ctx, JSValueConst global);\n"
"\n"
"/* the prototype and interface object of an interface in this context\n"
" * (new references; made on first use)\n"
" */\n"
"JSValue %1$s_proto(JSContext *ctx, const %1$s_class_t *klass);\n"
"JSValue %1$s_interface_object(JSContext *ctx, const %1$s_class_t *klass);\n"
"\n"
"/* a new object of an interface owning priv (freed with it, even when\n"
" * this fails: JS_EXCEPTION)\n"
" */\n"
"JSValue %1$s_new_object(JSContext *ctx, const %1$s_class_t *klass, void *priv);\n"
"\n"
"/* obj's private struct if obj implements the interface, else NULL */\n"
"void *%1$s_get_private(JSValueConst obj, const %1$s_class_t *klass);\n"
"bool %1$s_instanceof(JSValueConst obj, const %1$s_class_t *klass);\n"
"\n"
"/* as %1$s_get_private, a TypeError thrown when it is NULL */\n"
"void *%1$s_this_private(JSContext *ctx, JSValueConst obj, const %1$s_class_t *klass);\n"
"\n"
"/* from the binding's user: the per context storage of the prototypes and\n"
" * interface objects (2 * QJSKY_CLASS_COUNT values, JS_UNDEFINED at first),\n"
" * the global object's private struct\n"
" */\n"
"JSValue *%1$s_context_classes(JSContext *ctx);\n"
"void *%1$s_global_private(JSContext *ctx);\n"
"\n",
                QLPFX);

        return close_header(ir, bindc);
}

/**
 * generate binding source: the class table and the object model
 */
static int output_binding_src(struct ir *ir)
{
        int idx;
        struct opctx *bindc;

        if (output_open("binding.c", &bindc) != 0) {
                return -1;
        }

        output_tool_preface(bindc);
        output_method_cdata(bindc, ir->binding_node,
                            GENBIND_METHOD_TYPE_PREFACE);
        output_tool_prologue(bindc);
        output_method_cdata(bindc, ir->binding_node,
                            GENBIND_METHOD_TYPE_PROLOGUE);
        outputc(bindc, '\n');

        outputf(bindc,
                "const %1$s_class_t *const %1$s_classes[QJSKY_CLASS_COUNT] = {\n",
                QLPFX);
        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;
                char *index_name;

                if ((entry->type != IR_ENTRY_TYPE_INTERFACE) ||
                    !generated(entry)) {
                        continue;
                }
                index_name = qjs_class_index_name(entry);
                outputf(bindc, "\t[%s] = &%s_class_%s,\n",
                        index_name, QLPFX, entry->class_name);
                free(index_name);
        }
        outputf(bindc, "};\n\n");

        /* the interface objects on the global object, made on first read */
        outputf(bindc,
"static JSValue %1$s_lazy_interface(JSContext *ctx, const JSCFunctionListEntry *e)\n"
"{\n"
"\treturn %1$s_interface_object(ctx, e->u.lazy.opaque);\n"
"}\n"
"\n"
"static const JSCFunctionListEntry %1$s_interfaces[] = {\n",
                QLPFX);
        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;

                if ((entry->type != IR_ENTRY_TYPE_INTERFACE) ||
                    !generated(entry)) {
                        continue;
                }
                outputf(bindc,
                        "\tJS_LAZY_DEF(\"%s\", %s_lazy_interface, &%s_class_%s, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),\n",
                        entry->name, QLPFX, QLPFX, entry->class_name);
        }
        outputf(bindc, "};\n\n");

        outputf(bindc,
"JSClassID %1$s_class_id;\n"
"JSClassID %1$s_list_class_id;\n"
"\n"
"/* the object header of a DOM object (of either class), NULL if not one */\n"
"static inline %1$s_object_t *%1$s_object(JSValueConst obj)\n"
"{\n"
"\tJSClassID cid;\n"
"\tvoid *opaque = JS_GetAnyOpaque(obj, &cid);\n"
"\tif (cid != %1$s_class_id && cid != %1$s_list_class_id) {\n"
"\t\treturn NULL;\n"
"\t}\n"
"\treturn opaque;\n"
"}\n"
"\n"
"static void %1$s_finalizer(JSRuntime *rt, JSValueConst val)\n"
"{\n"
"\t%1$s_object_t *object = %1$s_object(val);\n"
"\tif (object != NULL) {\n"
"\t\tobject->klass->destroy(rt, object);\n"
"\t}\n"
"}\n"
"\n"
"static const JSClassDef %1$s_class_def = {\n"
"\t.class_name = \"Object\",\n"
"\t.finalizer = %1$s_finalizer,\n"
"};\n"
"\n"
"/* lists: the interface (of klass's chain) with the hook */\n"
"static const %1$s_class_t *%1$s_indexed(const %1$s_class_t *k)\n"
"{\n"
"\twhile (k != NULL && k->indexed_item == NULL) {\n"
"\t\tk = k->parent;\n"
"\t}\n"
"\treturn k;\n"
"}\n"
"\n"
"static const %1$s_class_t *%1$s_named(const %1$s_class_t *k)\n"
"{\n"
"\twhile (k != NULL && k->named_item == NULL) {\n"
"\t\tk = k->parent;\n"
"\t}\n"
"\treturn k;\n"
"}\n"
"\n"
"/* a list's length: -1 with an exception pending */\n"
"static int64_t %1$s_list_length(JSContext *ctx, const %1$s_class_t *k, JSValueConst obj)\n"
"{\n"
"\tJSValue v = k->indexed_length(ctx, obj);\n"
"\tint64_t len = 0;\n"
"\tif (JS_IsException(v)) {\n"
"\t\treturn -1;\n"
"\t}\n"
"\tif (JS_ToInt64(ctx, &len, v) < 0) {\n"
"\t\tlen = -1;\n"
"\t}\n"
"\tJS_FreeValue(ctx, v);\n"
"\treturn len;\n"
"}\n"
"\n"
"static int %1$s_list_get_own_property(JSContext *ctx, JSPropertyDescriptor *desc,\n"
"\t\t\t\t       JSValueConst obj, JSAtom prop)\n"
"{\n"
"\t%1$s_object_t *object = %1$s_object(obj);\n"
"\tconst %1$s_class_t *k;\n"
"\tJSValue key, v;\n"
"\tuint32_t idx;\n"
"\n"
"\tif (object == NULL) {\n"
"\t\treturn 0;\n"
"\t}\n"
"\tif (JS_AtomToArrayIndex(ctx, prop, &idx)) {\n"
"\t\t/* an index: item(i) below the length */\n"
"\t\tint64_t len;\n"
"\t\tk = %1$s_indexed(object->klass);\n"
"\t\tif (k == NULL) {\n"
"\t\t\treturn 0;\n"
"\t\t}\n"
"\t\tlen = %1$s_list_length(ctx, k, obj);\n"
"\t\tif (len < 0) {\n"
"\t\t\treturn -1;\n"
"\t\t}\n"
"\t\tif (idx >= len) {\n"
"\t\t\treturn 0;\n"
"\t\t}\n"
"\t\tkey = JS_NewUint32(ctx, idx);\n"
"\t\tv = k->indexed_item(ctx, obj, 1, (JSValueConst *)&key);\n"
"\t} else if (key = JS_AtomToValue(ctx, prop), JS_IsString(key)) {\n"
"\t\t/* a name: namedItem(name), if the prototypes do not have it */\n"
"\t\tJSValue proto;\n"
"\t\tint has;\n"
"\t\tk = %1$s_named(object->klass);\n"
"\t\tif (k == NULL) {\n"
"\t\t\tJS_FreeValue(ctx, key);\n"
"\t\t\treturn 0;\n"
"\t\t}\n"
"\t\tproto = JS_GetPrototype(ctx, obj);\n"
"\t\thas = JS_HasProperty(ctx, proto, prop);\n"
"\t\tJS_FreeValue(ctx, proto);\n"
"\t\tif (has != 0) {\n"
"\t\t\tJS_FreeValue(ctx, key);\n"
"\t\t\treturn has < 0 ? -1 : 0;\n"
"\t\t}\n"
"\t\tv = k->named_item(ctx, obj, 1, (JSValueConst *)&key);\n"
"\t\tJS_FreeValue(ctx, key);\n"
"\t\tif (JS_IsNull(v) || JS_IsUndefined(v)) {\n"
"\t\t\treturn 0;\n"
"\t\t}\n"
"\t} else {\n"
"\t\tJS_FreeValue(ctx, key);\n"
"\t\treturn 0;\n"
"\t}\n"
"\tif (JS_IsException(v)) {\n"
"\t\treturn -1;\n"
"\t}\n"
"\tif (desc != NULL) {\n"
"\t\tdesc->flags = JS_PROP_ENUMERABLE;\n"
"\t\tdesc->value = v;\n"
"\t\tdesc->getter = JS_UNDEFINED;\n"
"\t\tdesc->setter = JS_UNDEFINED;\n"
"\t} else {\n"
"\t\tJS_FreeValue(ctx, v);\n"
"\t}\n"
"\treturn 1;\n"
"}\n"
"\n"
"/* a list's own names: its indexes (its names are not enumerable) */\n"
"static int %1$s_list_get_own_property_names(JSContext *ctx, JSPropertyEnum **ptab,\n"
"\t\t\t\t\t     uint32_t *plen, JSValueConst obj)\n"
"{\n"
"\t%1$s_object_t *object = %1$s_object(obj);\n"
"\tconst %1$s_class_t *k = object != NULL ? %1$s_indexed(object->klass) : NULL;\n"
"\tint64_t len = 0, i;\n"
"\tJSPropertyEnum *tab;\n"
"\n"
"\tif (k != NULL && (len = %1$s_list_length(ctx, k, obj)) < 0) {\n"
"\t\treturn -1;\n"
"\t}\n"
"\ttab = js_malloc(ctx, sizeof(*tab) * (len > 0 ? len : 1));\n"
"\tif (tab == NULL) {\n"
"\t\treturn -1;\n"
"\t}\n"
"\tfor (i = 0; i < len; i++) {\n"
"\t\ttab[i].is_enumerable = true;\n"
"\t\ttab[i].atom = JS_NewAtomUInt32(ctx, (uint32_t)i);\n"
"\t}\n"
"\t*ptab = tab;\n"
"\t*plen = (uint32_t)len;\n"
"\treturn 0;\n"
"}\n"
"\n"
"static JSClassExoticMethods %1$s_list_exotic = {\n"
"\t.get_own_property = %1$s_list_get_own_property,\n"
"\t.get_own_property_names = %1$s_list_get_own_property_names,\n"
"};\n"
"\n"
"static const JSClassDef %1$s_list_class_def = {\n"
"\t.class_name = \"Object\",\n"
"\t.finalizer = %1$s_finalizer,\n"
"\t.exotic = &%1$s_list_exotic,\n"
"};\n"
"\n"
"int %1$s_runtime_init(JSRuntime *rt)\n"
"{\n"
"\tif (%1$s_class_id == 0) {\n"
"\t\tJS_NewClassID(rt, &%1$s_class_id);\n"
"\t\tJS_NewClassID(rt, &%1$s_list_class_id);\n"
"\t}\n"
"\tif (JS_NewClass(rt, %1$s_class_id, &%1$s_class_def) < 0) {\n"
"\t\treturn -1;\n"
"\t}\n"
"\treturn JS_NewClass(rt, %1$s_list_class_id, &%1$s_list_class_def);\n"
"}\n"
"\n"
"int %1$s_define_interfaces(JSContext *ctx, JSValueConst global)\n"
"{\n"
"\treturn JS_SetPropertyFunctionList(ctx, global, %1$s_interfaces,\n"
"\t\t\t\t\t  sizeof(%1$s_interfaces) / sizeof(%1$s_interfaces[0]));\n"
"}\n"
"\n"
"static JSValue %1$s_illegal_constructor(JSContext *ctx, JSValueConst new_target,\n"
"\t\t\t\t\tint argc, JSValueConst *argv)\n"
"{\n"
"\treturn JS_ThrowTypeError(ctx, \"Illegal constructor\");\n"
"}\n"
"\n"
"/* the prototype and interface object of klass, made together */\n"
"static int %1$s_make_class(JSContext *ctx, const %1$s_class_t *klass)\n"
"{\n"
"\tJSValue *classes = %1$s_context_classes(ctx);\n"
"\tJSValue parent_proto, parent_ctor, proto, ctor;\n"
"\n"
"\tif (klass->parent != NULL) {\n"
"\t\tparent_proto = %1$s_proto(ctx, klass->parent);\n"
"\t\tif (JS_IsException(parent_proto)) {\n"
"\t\t\treturn -1;\n"
"\t\t}\n"
"\t\tparent_ctor = %1$s_interface_object(ctx, klass->parent);\n"
"\t\tif (JS_IsException(parent_ctor)) {\n"
"\t\t\tJS_FreeValue(ctx, parent_proto);\n"
"\t\t\treturn -1;\n"
"\t\t}\n"
"\t\tproto = JS_NewObjectProto(ctx, parent_proto);\n"
"\t\tJS_FreeValue(ctx, parent_proto);\n"
"\t} else {\n"
"\t\tparent_ctor = JS_UNDEFINED;\n"
"\t\tproto = JS_NewObject(ctx);\n"
"\t}\n"
"\tif (JS_IsException(proto)) {\n"
"\t\tJS_FreeValue(ctx, parent_ctor);\n"
"\t\treturn -1;\n"
"\t}\n"
"\tif (JS_SetPropertyFunctionList(ctx, proto, klass->proto_funcs,\n"
"\t\t\t\t       klass->proto_funcs_count) < 0 ||\n"
"\t    (klass->proto_init != NULL && klass->proto_init(ctx, proto) < 0)) {\n"
"\t\tgoto fail_proto;\n"
"\t}\n"
"\n"
"\tctor = JS_NewCFunction2(ctx, (JSCFunction *)%1$s_illegal_constructor,\n"
"\t\t\t\tklass->name, 0, JS_CFUNC_constructor_or_func, 0);\n"
"\tif (JS_IsException(ctor)) {\n"
"\t\tgoto fail_proto;\n"
"\t}\n"
"\tif ((!JS_IsUndefined(parent_ctor) &&\n"
"\t     JS_SetPrototype(ctx, ctor, parent_ctor) < 0) ||\n"
"\t    JS_SetPropertyFunctionList(ctx, ctor, klass->ctor_funcs,\n"
"\t\t\t\t       klass->ctor_funcs_count) < 0 ||\n"
"\t    JS_SetConstructor(ctx, ctor, proto) < 0) {\n"
"\t\tJS_FreeValue(ctx, ctor);\n"
"\t\tgoto fail_proto;\n"
"\t}\n"
"\tJS_FreeValue(ctx, parent_ctor);\n"
"\n"
"\tclasses[klass->index] = proto;\n"
"\tclasses[QJSKY_CLASS_COUNT + klass->index] = ctor;\n"
"\treturn 0;\n"
"\n"
"fail_proto:\n"
"\tJS_FreeValue(ctx, parent_ctor);\n"
"\tJS_FreeValue(ctx, proto);\n"
"\treturn -1;\n"
"}\n"
"\n"
"JSValue %1$s_proto(JSContext *ctx, const %1$s_class_t *klass)\n"
"{\n"
"\tJSValue *classes = %1$s_context_classes(ctx);\n"
"\tif (JS_IsUndefined(classes[klass->index]) &&\n"
"\t    %1$s_make_class(ctx, klass) < 0) {\n"
"\t\treturn JS_EXCEPTION;\n"
"\t}\n"
"\treturn JS_DupValue(ctx, classes[klass->index]);\n"
"}\n"
"\n"
"JSValue %1$s_interface_object(JSContext *ctx, const %1$s_class_t *klass)\n"
"{\n"
"\tJSValue *classes = %1$s_context_classes(ctx);\n"
"\tif (JS_IsUndefined(classes[klass->index]) &&\n"
"\t    %1$s_make_class(ctx, klass) < 0) {\n"
"\t\treturn JS_EXCEPTION;\n"
"\t}\n"
"\treturn JS_DupValue(ctx, classes[QJSKY_CLASS_COUNT + klass->index]);\n"
"}\n"
"\n"
"JSValue %1$s_new_object(JSContext *ctx, const %1$s_class_t *klass, void *priv)\n"
"{\n"
"\tJSValue proto, obj;\n"
"\n"
"\t((%1$s_object_t *)priv)->klass = klass;\n"
"\tproto = %1$s_proto(ctx, klass);\n"
"\tif (JS_IsException(proto)) {\n"
"\t\tfree(priv);\n"
"\t\treturn JS_EXCEPTION;\n"
"\t}\n"
"\tobj = JS_NewObjectProtoClass(ctx, proto,\n"
"\t\t(%1$s_indexed(klass) != NULL || %1$s_named(klass) != NULL) ?\n"
"\t\t%1$s_list_class_id : %1$s_class_id);\n"
"\tJS_FreeValue(ctx, proto);\n"
"\tif (JS_IsException(obj)) {\n"
"\t\tfree(priv);\n"
"\t\treturn JS_EXCEPTION;\n"
"\t}\n"
"\tJS_SetOpaque(obj, priv);\n"
"\treturn obj;\n"
"}\n"
"\n"
"void *%1$s_get_private(JSValueConst obj, const %1$s_class_t *klass)\n"
"{\n"
"\t%1$s_object_t *object = %1$s_object(obj);\n"
"\tconst %1$s_class_t *k;\n"
"\n"
"\tif (object == NULL) {\n"
"\t\treturn NULL;\n"
"\t}\n"
"\tfor (k = object->klass; k != NULL; k = k->parent) {\n"
"\t\tif (k == klass) {\n"
"\t\t\treturn object;\n"
"\t\t}\n"
"\t}\n"
"\treturn NULL;\n"
"}\n"
"\n"
"bool %1$s_instanceof(JSValueConst obj, const %1$s_class_t *klass)\n"
"{\n"
"\treturn %1$s_get_private(obj, klass) != NULL;\n"
"}\n"
"\n"
"void *%1$s_this_private(JSContext *ctx, JSValueConst obj, const %1$s_class_t *klass)\n"
"{\n"
"\tvoid *priv = %1$s_get_private(obj, klass);\n"
"\tif (priv == NULL) {\n"
"\t\tJS_ThrowTypeError(ctx, \"Illegal invocation (not a %%s)\", klass->name);\n"
"\t}\n"
"\treturn priv;\n"
"}\n"
"\n",
                QLPFX);

        output_method_cdata(bindc, ir->binding_node,
                            GENBIND_METHOD_TYPE_POSTFACE);
        output_close(bindc);

        return 0;
}

static int output_interfaces_dictionaries(struct ir *ir)
{
        int res;
        int idx;

        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;

                switch (entry->type) {
                case IR_ENTRY_TYPE_INTERFACE:
                        if (generated(entry)) {
                                res = qjs_output_interface(ir, entry);
                                if (res != 0) {
                                        return res;
                                }
                        }
                        break;

                case IR_ENTRY_TYPE_DICTIONARY:
                        res = qjs_output_dictionary(ir, entry);
                        if (res != 0) {
                                return res;
                        }
                        break;
                }
        }

        return 0;
}

/* exported interface documented in qjs-libdom.h */
int qjs_libdom_output(struct ir *ir)
{
        int idx;
        int res;

        for (idx = 0; idx < ir->entryc; idx++) {
                struct ir_entry *entry = ir->entries + idx;

                entry->class_name = gen_idl2c_name(entry->name);
                if (entry->class_name != NULL) {
                        int len = strlen(entry->class_name) + 3;
                        entry->filename = malloc(len);
                        snprintf(entry->filename, len, "%s.c",
                                 entry->class_name);
                }
        }

        res = output_interfaces_dictionaries(ir);
        if (res == 0) {
                res = output_private_header(ir);
        }
        if (res == 0) {
                res = output_prototype_header(ir);
        }
        if (res == 0) {
                res = output_binding_header(ir);
        }
        if (res == 0) {
                res = output_binding_src(ir);
        }
        if (res == 0) {
                res = output_makefile(ir);
        }

        return res;
}
