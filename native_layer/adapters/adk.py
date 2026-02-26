import logging
import json
import array
import inspect
from typing import List, Optional, Any, Dict, Callable, Union
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
        return "q"   # int64
    return "d"       # float64 default


def _to_native(raw: Any, arg_schema: dict) -> Any:
    """Convert a Python value to the correct native type for the C bridge."""
    kind = arg_schema.get("type", "array")
    if kind == "array":
        items = arg_schema.get("items", {})
        tc = _array_typecode(items)
        return memoryview(array.array(tc, raw if raw is not None else []))
    if kind == "integer":
        return int(raw) if raw is not None else 0
    if kind == "number":
        return float(raw) if raw is not None else 0.0
    if kind == "string":
        return str(raw) if raw is not None else ""
    return raw


def _py_type(arg_schema: dict) -> type:
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

def _build_docstring(description: str, props: dict) -> str:
    """Build a Google-style docstring ADK parses for parameter descriptions."""
    lines = [description, "", "Args:"]
    for name, schema in props.items():
        type_label = schema.get("type", "any")
        param_desc = schema.get("description", "")
        lines.append(f"    {name} ({type_label}): {param_desc}")
    lines += ["", "Returns:", "    dict: {'status': 'success', 'result': ...}"]
    return "\n".join(lines)


def _make_tool_function(
    plugin_name: str,
    func_name: str,
    description: str,
    props: dict,
    manager: Any,
) -> Callable:
    """
    Build a plain callable whose inspect.Signature exactly matches the
    plugin's JSON schema.

    ADK calls inspect.signature() on the function to generate the Gemini
    function declaration. **kwargs is explicitly ignored by ADK — every
    parameter must be a named inspect.Parameter with the correct annotation.
    We attach a real __signature__ so inspect.signature() returns the right
    thing regardless of how the function body is actually defined.
    """
    parameters = [
        inspect.Parameter(
            name=param_name,
            kind=inspect.Parameter.POSITIONAL_OR_KEYWORD,
            annotation=_py_type(param_schema),
            # No default → ADK treats as required
        )
        for param_name, param_schema in props.items()
    ]

    real_sig = inspect.Signature(
        parameters=parameters,
        return_annotation=Dict[str, Any],
    )

    def _execute(**kwargs) -> Dict[str, Any]:
        logger.debug("Native call: %s::%s | args=%s", plugin_name, func_name, kwargs)
        native_inputs = [
            _to_native(kwargs.get(k), s) for k, s in props.items()
        ]
        result = manager.execute(plugin_name, func_name, native_inputs)
        return {"status": "success", "result": result}

    _execute.__name__      = f"{plugin_name}_{func_name}"
    _execute.__qualname__  = f"{plugin_name}_{func_name}"
    _execute.__doc__       = _build_docstring(description, props)
    _execute.__signature__ = real_sig

    return _execute


def _make_function_tool(tool_fn: Callable, tool_name: str, description: str) -> FunctionTool:
    """
    Construct a FunctionTool using the only pattern confirmed to work by the
    ADK maintainers:

    tool = FunctionTool(fn)
    tool.name = "..."
    tool.description = "..."

    Passing func= as a keyword argument, or passing name=/description= to
    __init__, raises TypeError in ADK 1.2.1 through 1.11.0.
    """
    tool = FunctionTool(tool_fn)
    tool.name = tool_name
    tool.description = description
    return tool


class NativeADKToolset(BaseToolset):
    """
    Bridges a NativeManager (hot-reloadable C++ plugins) into ADK.
    """

    def __init__(
        self,
        manager: Any,
        tool_name_prefix: str = "native",
        tool_filter: Optional[Union[list, Any]] = None,
    ) -> None:
        super().__init__(
            tool_name_prefix=tool_name_prefix,
            tool_filter=tool_filter,
        )   
        self.manager = manager
        logger.info("NativeADKToolset initialised (prefix=%r).", tool_name_prefix)

    async def get_tools(
        self,
        readonly_context: Optional[Any] = None,
    ) -> List[FunctionTool]:
        """
        Called by ADK before every agent turn. Returns one FunctionTool per
        exported function found across all active plugins.
        """
        adk_tools: List[FunctionTool] = []

        for plugin_name in self.manager.get_active_tools():
            raw_schema = self.manager.get_schema(plugin_name)

            try:
                schema_data = json.loads(raw_schema)
            except json.JSONDecodeError as exc:
                logger.error("Bad schema for plugin '%s': %s", plugin_name, exc)
                continue

            functions = schema_data if isinstance(schema_data, list) else [schema_data]

            for func_def in functions:
                func_name = func_def.get("name")
                if not func_name:
                    logger.warning(
                        "Skipping unnamed func_def in plugin '%s'", plugin_name
                    )
                    continue

                description = func_def.get("description", "")
                props = func_def.get("parameters", {}).get("properties", {})
                tool_name = f"{plugin_name}_{func_name}"

                tool_fn = _make_tool_function(
                    plugin_name=plugin_name,
                    func_name=func_name,
                    description=description,
                    props=props,
                    manager=self.manager,
                )

                adk_tool = _make_function_tool(tool_fn, tool_name, description)
                adk_tools.append(adk_tool)
                logger.info("Registered tool: %s", tool_name)

        return adk_tools

    async def close(self) -> None:
        self.manager.stop_watching()
        logger.info("NativeADKToolset shut down.")