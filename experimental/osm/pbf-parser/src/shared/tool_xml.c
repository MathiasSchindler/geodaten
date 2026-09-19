#include "runtime.h"
#include "tool_util.h"
#include "xml.h"

int tool_xml_name_stack_push(XmlNameStack *stack, XmlName name, const char *tool_name) {
    if (xml_name_stack_push(stack, name) != 0) {
        tool_write_error(tool_name, "out of memory", 0);
        return -1;
    }
    return 0;
}

int tool_xml_key_parse(const char *text, ToolXmlKeySpec *spec, const char *tool_name) {
    if (text == 0 || spec == 0 || text[0] == '\0') {
        tool_write_error(tool_name, "invalid key: ", text);
        return -1;
    }
    if (text[0] == '@') {
        if (!xml_is_name(text + 1U)) {
            tool_write_error(tool_name, "invalid key attribute: ", text);
            return -1;
        }
        spec->kind = TOOL_XML_KEY_ATTR;
        spec->name = text + 1U;
        return 0;
    }
    if (text[0] == '.' && text[1] == '\0') {
        spec->kind = TOOL_XML_KEY_TEXT;
        spec->name = 0;
        return 0;
    }
    if (!xml_is_name(text)) {
        tool_write_error(tool_name, "invalid key child: ", text);
        return -1;
    }
    spec->kind = TOOL_XML_KEY_CHILD;
    spec->name = text;
    return 0;
}

int tool_xml_selector_compile(XmlSelector *selector_out, const char *selector, const char *tool_name) {
    if (xml_selector_compile(selector_out, selector) != 0) {
        tool_write_error(tool_name, "invalid selector: ", selector);
        return -1;
    }
    return 0;
}

void tool_xml_key_state_init(ToolXmlKeyState *state) {
    state->key = "";
    state->key_length = 0U;
    state->active_child_depth = 0U;
    state->found = 0;
}

void tool_xml_key_start(const ToolXmlKeySpec *spec, const XmlToken *token, unsigned int depth, unsigned int capture_depth, ToolXmlKeyState *state) {
    if (spec->kind == TOOL_XML_KEY_ATTR && depth == capture_depth) {
        xml_token_attr_slice(token, spec->name, &state->key, &state->key_length);
        state->found = 1;
    } else if (spec->kind == TOOL_XML_KEY_CHILD && !state->found && depth == capture_depth + 1U && xml_name_equals(&token->name, spec->name)) {
        state->key = "";
        state->key_length = 0U;
        state->found = 1;
        if (token->type == XML_TOKEN_START) state->active_child_depth = depth;
    }
}

void tool_xml_key_text(const ToolXmlKeySpec *spec, const XmlToken *token, unsigned int depth, unsigned int capture_depth, ToolXmlKeyState *state) {
    if (token->text_is_blank) return;
    if (spec->kind == TOOL_XML_KEY_TEXT && !state->found && depth == capture_depth) {
        state->key = token->text;
        state->key_length = token->text_length;
        state->found = 1;
    } else if (spec->kind == TOOL_XML_KEY_CHILD && state->active_child_depth != 0U && depth == state->active_child_depth && !state->found) {
        state->key = token->text;
        state->key_length = token->text_length;
        state->found = 1;
    } else if (spec->kind == TOOL_XML_KEY_CHILD && state->active_child_depth != 0U && depth == state->active_child_depth && state->key_length == 0U) {
        state->key = token->text;
        state->key_length = token->text_length;
    }
}

void tool_xml_key_end(const ToolXmlKeySpec *spec, unsigned int depth, ToolXmlKeyState *state) {
    if (spec->kind == TOOL_XML_KEY_CHILD && state->active_child_depth == depth) {
        state->active_child_depth = 0U;
    }
}