#ifndef NEWOS_XML_DTD_H
#define NEWOS_XML_DTD_H

#include "xml.h"

typedef enum {
    XML_DTD_DEFAULT_NONE = 0,
    XML_DTD_DEFAULT_IMPLIED,
    XML_DTD_DEFAULT_REQUIRED,
    XML_DTD_DEFAULT_VALUE,
    XML_DTD_DEFAULT_FIXED
} XmlDtdDefaultKind;

typedef struct {
    char *name;
    char *content;
    int is_empty;
    int is_any;
} XmlDtdElementDecl;

typedef struct {
    char *element_name;
    char *name;
    char *type;
    XmlDtdDefaultKind default_kind;
    char *default_value;
} XmlDtdAttributeDecl;

typedef struct {
    char *root_name;
    XmlDtdElementDecl *elements;
    unsigned int element_count;
    unsigned int element_capacity;
    XmlDtdAttributeDecl *attributes;
    unsigned int attribute_count;
    unsigned int attribute_capacity;
} XmlDtd;

void xml_dtd_init(XmlDtd *dtd);
void xml_dtd_free(XmlDtd *dtd);
int xml_dtd_parse_subset(XmlDtd *dtd, const char *text, size_t length, const char *tool_name);
int xml_dtd_parse_doctype(XmlDtd *dtd, const char *text, size_t length, const char *tool_name);
int xml_dtd_load(XmlDtd *dtd, const char *dtd_path, const char *document_text, size_t document_length, const char *tool_name);
const XmlDtdElementDecl *xml_dtd_find_element(const XmlDtd *dtd, const XmlName *name);
const XmlDtdAttributeDecl *xml_dtd_find_attribute(const XmlDtd *dtd, const XmlName *element_name, const XmlName *attribute_name);
int xml_dtd_validate_document(const XmlDtd *dtd, const char *path, const char *input, size_t length, const char *tool_name);
void xml_dtd_write_defaulted_start(int fd, const XmlDtd *dtd, const XmlToken *token);
void xml_dtd_write_info(int fd, const XmlDtd *dtd);

#endif