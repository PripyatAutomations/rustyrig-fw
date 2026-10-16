#!/usr/bin/env bash
# Check the shared source inventory's lexical counting against tricky C tokens.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 - <<'PY'
import importlib.util
spec = importlib.util.spec_from_file_location('source_size', 'tools/source-size.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = '''/* license
ignored */
// ignored
static const char *example(int x) {
   const char *url = "https://host/*literal*/"; // trailing comment
   char quote = '\\'';
   /* { not a function brace } */

   return x ? url : "}";
}
'''
result = module.inspect_source(source)
assert result['code_lines'] == 5, result
assert result['functions'] == [{'name': 'example', 'line': 4, 'code_lines': 5}], result
assert module.inspect_source('int declaration(void);\n')['functions'] == []
print('PASS: source size excludes comments/blanks and preserves literals/function spans')
PY

# Enforce the hard file limit across the inventoried owned C components.
python3 tools/source-size.py --min-lines 1501 --function-lines 100000 --max-lines 1500
