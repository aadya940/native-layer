import json

def to_ollama_tools(manager):
    """Convert native plugins to Ollama tool format.

    This class is meant to be inside a infinite loop for
    hot reload of plugins so it can get fresh tools.
    """
    tools = []
    for plugin in manager.get_active_tools():
        schema = json.loads(manager.get_schema(plugin))
        functions = schema if isinstance(schema, list) else [schema]
        
        for func in functions:
            tools.append({
                'type': 'function',
                'function': {
                    'name': func['name'],
                    'description': func.get('description', ''),
                    'parameters': func.get('parameters', {})
                }
            })
    return tools