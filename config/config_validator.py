import json
import sys

import jsonschema
from jsonschema import validate

def load_json(file_path):
    """Load JSON data from a file."""
    with open(file_path, 'r') as f:
        return json.load(f)

def validate_config(config_path, schema_path):
    """Validate a configuration file against the JSON schema."""
    try:
        config = load_json(config_path)
        schema = load_json(schema_path)
        validate(instance=config, schema=schema)
        print("Validation successful: The configuration file is valid.")
    except jsonschema.exceptions.ValidationError as e:
        print(f"Validation error: {e.message}")
    except jsonschema.exceptions.SchemaError as e:
        print(f"Schema error: {e.message}")
    except Exception as e:
        print(f"Unexpected error: {e}")

if __name__ == "__main__":
    config_file = sys.argv[1]
    schema_file = sys.argv[2]
    validate_config(config_file, schema_file)