import os
import sys
import argparse
import json
import jsonschema
import ruamel.yaml

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cfg", help="Filename YAML input file")
    ap.add_argument('-s', '--schema', help="schema file")
    args = ap.parse_args()

    yaml = ruamel.yaml.YAML(typ='safe')
    yaml.allow_duplicate_keys = False

    with open(args.schema, 'r', encoding='utf-8') as f:
        schema = yaml.load(f.read())

    with open(args.cfg, 'r', encoding='utf-8') as f:
        cfg = yaml.load(f.read())

    jsonschema.validate(cfg, schema=schema)
