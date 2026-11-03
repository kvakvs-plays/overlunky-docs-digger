"""Checks docs-digger output against schema/docs-digger.schema.json (standard library only).

Usage: python tools/check_schema.py <schema> <output.json> [...]

Supports the JSON Schema keywords the schema uses ($ref into $defs, type, const, enum, properties, required,
additionalProperties: false, items) and refuses any other, so nothing is silently ignored. Every property must be
described (directly or by the definition it refers to): the schema is the field documentation. Exits with 1 on the
first problem of each file.
"""
import json
import sys

# Keywords this checker understands; the schema may use no others.
KEYWORDS = {'$schema', '$id', '$ref', '$defs', 'title', 'description', 'type', 'const', 'enum', 'properties',
            'required', 'additionalProperties', 'items'}

# JSON Schema type names and the Python types json.load gives for them (bool is not an integer here).
TYPES = {'object': dict, 'array': list, 'string': str, 'boolean': bool, 'integer': int}


class SchemaError(Exception):
    """A value that does not match the schema, or a schema the checker cannot use."""


def resolve(root, node):
    """The definition a `$ref: #/$defs/Name` points to, or node itself."""
    ref = node.get('$ref')
    if ref is None:
        return node
    if not ref.startswith('#/$defs/'):
        raise SchemaError(f'unsupported $ref {ref}')
    return root['$defs'][ref[len('#/$defs/'):]]


def check_schema(root, node, path):
    """Refuses unknown keywords and properties without a description, everywhere in the schema."""
    unknown = set(node) - KEYWORDS
    if unknown:
        raise SchemaError(f'{path}: unsupported keywords {sorted(unknown)}')
    for name, prop in node.get('properties', {}).items():
        if 'description' not in prop and 'description' not in resolve(root, prop):
            raise SchemaError(f'{path}.{name}: no description')
        check_schema(root, prop, f'{path}.{name}')
    if 'items' in node:
        check_schema(root, node['items'], f'{path}[]')
    for name, definition in node.get('$defs', {}).items():
        check_schema(root, definition, f'$defs.{name}')


def type_matches(value, names):
    """True when value has one of the JSON types named (a string or a list of them)."""
    names = [names] if isinstance(names, str) else names
    return any(isinstance(value, TYPES[n]) and not (n == 'integer' and isinstance(value, bool)) for n in names)


def check_object(root, node, value, path):
    """Required, described and only described fields, each checked against its schema."""
    missing = [name for name in node.get('required', []) if name not in value]
    if missing:
        raise SchemaError(f'{path}: missing {missing}')
    properties = node.get('properties', {})
    extra = [name for name in value if name not in properties]
    if extra and node.get('additionalProperties') is False:
        raise SchemaError(f'{path}: fields not in the schema {extra}')
    for name, item in value.items():
        if name in properties:
            check(root, properties[name], item, f'{path}.{name}')


def check(root, node, value, path):
    """Checks value against the schema node, raising SchemaError at the first mismatch."""
    if '$ref' in node:
        check(root, resolve(root, node), value, path)
    if 'type' in node and not type_matches(value, node['type']):
        raise SchemaError(f'{path}: expected {node["type"]}, got {type(value).__name__}')
    if 'const' in node and value != node['const']:
        raise SchemaError(f'{path}: expected {node["const"]!r}, got {value!r}')
    if 'enum' in node and value not in node['enum']:
        raise SchemaError(f'{path}: {value!r} is not one of {node["enum"]}')
    if isinstance(value, dict) and ('properties' in node or 'required' in node):
        check_object(root, node, value, path)
    if isinstance(value, list) and 'items' in node:
        for index, item in enumerate(value):
            check(root, node['items'], item, f'{path}[{index}]')


def main():
    """Checks the schema itself, then every output file given."""
    with open(sys.argv[1], encoding='utf-8') as f:
        schema = json.load(f)
    failed = False
    try:
        check_schema(schema, schema, '$')
    except SchemaError as error:
        print(f'{sys.argv[1]}: {error}')
        return 1
    for name in sys.argv[2:]:
        with open(name, encoding='utf-8') as f:
            output = json.load(f)
        try:
            check(schema, schema, output, '$')
        except SchemaError as error:
            print(f'{name}: {error}')
            failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
