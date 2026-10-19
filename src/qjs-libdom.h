/* QuickJS binding generation
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef nsgenbind_qjs_libdom_h
#define nsgenbind_qjs_libdom_h

/** prefix for all generated functions */
#define QLPFX "qjsky"

int qjs_libdom_output(struct ir *ir);

int qjs_output_interface(struct ir *ir, struct ir_entry *interfacee);
int qjs_output_dictionary(struct ir *ir, struct ir_entry *dictionarye);
int qjs_output_interface_declaration(struct opctx *outc, struct ir_entry *interfacee);
int qjs_output_dictionary_declaration(struct opctx *outc, struct ir_entry *dictionarye);

int qjs_output_generated_attribute_getter(struct opctx *outc, struct ir_entry *interfacee, struct ir_attribute_entry *atributee);
int qjs_output_generated_attribute_setter(struct opctx *outc, struct ir_entry *interfacee, struct ir_attribute_entry *atributee);

/** the upper case class index name (QJSKY_CLASS_...) for an entry */
char *qjs_class_index_name(const struct ir_entry *entry);

/** the entry for an interface named in a type, NULL if none generated */
struct ir_entry *qjs_find_interface(struct ir *ir, const char *name);

/* the language independent pieces shared with the duktape backend
 * (duk-libdom-common.c)
 */
int output_tool_preface(struct opctx *outc);
int output_tool_prologue(struct opctx *outc);
int output_cdata(struct opctx *outc, struct genbind_node *node, enum genbind_node_type nodetype);
int output_ccode(struct opctx *outc, struct genbind_node *node);
int output_method_cdata(struct opctx *outc, struct genbind_node *node, enum genbind_method_type sel_method_type);
int output_ctype(struct opctx *outc, struct genbind_node *node, bool identifier);
char *gen_idl2c_name(const char *idlname);

#endif
