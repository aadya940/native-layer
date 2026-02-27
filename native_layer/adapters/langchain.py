import logging
import json
from typing import List, Callable, Any, Type
from pydantic import BaseModel, create_model
from langchain_core.tools import StructuredTool
from langchain.agents.middleware import AgentMiddleware, ModelRequest
from langchain.agents.middleware.types import ModelResponse

from .adk import _to_native, _py_type

logger = logging.getLogger(__name__)

_JSON_TO_PYDANTIC: dict[str, Any] = {
    "number":  float,
    "integer": int,
    "string":  str,
    "boolean": bool,
}


class NativeHotReloadMiddleware(AgentMiddleware):
    def __init__(self, manager):
        self.manager = manager
        logger.info("LangChain Native Hot-Reload Middleware initialized.")

    def _build_pydantic_model(
        self, tool_name: str, parameters: dict
    ) -> Type[BaseModel]:
        """Dynamically builds a Pydantic model from the C++ JSON schema properties."""
        fields: dict[str, Any] = {}
        for arg_name, arg_schema in parameters.get("properties", {}).items():
            py_type = _py_type(arg_schema)
            fields[arg_name] = (py_type, ...)
        return create_model(f"{tool_name}_schema", **fields)

    def _get_live_tools(self) -> List[StructuredTool]:
        langchain_tools = []
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
                    description = func_def.get(
                        "description", "Native C++ execution bridge."
                    )
                    parameters  = func_def.get("parameters", {})
                    props       = parameters.get("properties", {})

                    args_schema = self._build_pydantic_model(func_name, parameters)

                    def create_executor(
                        p_name=plugin_name,
                        f_name=func_name,
                        f_props=props,
                    ):
                        def execute_native(**kwargs):
                            logger.debug(
                                "Executing native routine: %s::%s", p_name, f_name
                            )
                            native_inputs = [
                                _to_native(kwargs.get(k), s)
                                for k, s in f_props.items()
                            ]
                            return self.manager.execute(p_name, f_name, native_inputs)

                        return execute_native

                    tool = StructuredTool.from_function(
                        func=create_executor(),
                        name=f"{plugin_name}_{func_name}",
                        description=description,
                        args_schema=args_schema,
                    )
                    langchain_tools.append(tool)

            except json.JSONDecodeError as e:
                logger.error(
                    "Failed to parse schema for plugin '%s': %s", plugin_name, e
                )

        return langchain_tools

    def wrap_model_call(
        self,
        request: ModelRequest,
        handler: Callable[[ModelRequest], ModelResponse],
    ) -> ModelResponse:

        request.tools = self._get_live_tools()
        return handler(request)
