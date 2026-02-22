import logging
import json
import array
from typing import List, Optional, Any, Dict
from google.adk.tools.base_toolset import BaseToolset
from google.adk.tools import FunctionTool

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


def _array_typecode(items_schema: dict) -> str:
    """Return the array.array typecode for a JSON Schema items descriptor."""
    fmt = items_schema.get("format", "")
    itype = items_schema.get("type", "number")
    if fmt == "float32":
        return "f"
    if itype == "integer":
        return "q"  # int64
    return "d"      # float64 default


def _to_native(raw: Any, arg_schema: dict) -> Any:
    """Convert a Python value to the correct native type for the C bridge."""
    kind = arg_schema.get("type", "array")
    if kind == "array":
        items = arg_schema.get("items", {})
        tc = _array_typecode(items)
        return memoryview(array.array(tc, raw if raw is not None else []))
    elif kind == "integer":
        return int(raw) if raw is not None else 0
    elif kind == "number":
        return float(raw) if raw is not None else 0.0
    elif kind == "string":
        return str(raw) if raw is not None else ""
    return raw


def _py_annotation(arg_schema: dict):
    """Return the Python type annotation corresponding to a JSON Schema entry."""
    kind = arg_schema.get("type", "array")
    if kind == "array":
        items_type = arg_schema.get("items", {}).get("type", "number")
        return List[int] if items_type == "integer" else List[float]
    if kind == "integer":
        return int
    if kind == "number":
        return float
    return str


class NativeADKToolset(BaseToolset):
    def __init__(self, manager):
        self.manager = manager
        logger.info("Native C++ ADK Toolset initialized.")

    async def get_tools(
        self, readonly_context: Optional[Any] = None
    ) -> List[FunctionTool]:
        adk_tools = []
        active_plugins = self.manager.get_active_tools()

        for plugin_name in active_plugins:
            raw_schema = self.manager.get_schema(plugin_name)

            try:
                schema_data = json.loads(raw_schema)
                functions = (
                    schema_data if isinstance(schema_data, list) else [schema_data]
                )

                for func_def in functions:
                    func_name   = func_def.get("name")
                    description = func_def.get("description", "")
                    props       = func_def.get("parameters", {}).get("properties", {})

                    def create_native_caller(
                        p_name=plugin_name,
                        f_name=func_name,
                        f_props=props,
                    ):
                        def execute_native(**kwargs) -> Dict[str, Any]:
                            logger.debug(
                                "Executing native routine: %s::%s", p_name, f_name
                            )
                            native_inputs = [
                                _to_native(kwargs.get(k), s)
                                for k, s in f_props.items()
                            ]
                            result = self.manager.execute(p_name, f_name, native_inputs)
                            return {"status": "success", "result": result}

                        execute_native.__name__ = f"{p_name}_{f_name}"
                        execute_native.__doc__  = description
                        execute_native.__annotations__ = {
                            k: _py_annotation(s)
                            for k, s in f_props.items()
                        }
                        execute_native.__annotations__["return"] = Dict[str, Any]
                        return execute_native

                    adk_tool = FunctionTool(
                        func=create_native_caller(),
                        name=f"{plugin_name}_{func_name}",
                    )
                    adk_tools.append(adk_tool)

            except json.JSONDecodeError as e:
                logger.error(
                    "Failed to parse schema for plugin '%s': %s", plugin_name, e
                )

        return adk_tools

    async def close(self) -> None:
        self.manager.stop_watching()
        logger.info("Native ADK Toolset shut down safely.")
