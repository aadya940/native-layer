"""
LangChain adapter integration tests.

Tests the full stack: NativeHotReloadMiddleware → StructuredTool → manager.execute()
No LLM required, tools are invoked directly.

Run from project root: pytest
"""
import pytest
from typing import List


@pytest.fixture(scope="module")
def middleware(manager):
    from native_layer.adapters.langchain import NativeHotReloadMiddleware
    return NativeHotReloadMiddleware(manager)


@pytest.fixture(scope="module")
def tools(middleware):
    return {t.name: t for t in middleware._get_live_tools()}



class TestToolDiscovery:
    def test_tools_returned(self, tools):
        assert len(tools) > 0

    def test_expected_tool_names(self, tools):
        assert "math_double_array" in tools
        assert "math_scale_array"  in tools
        assert "math_add_arrays"   in tools

    def test_tools_are_structured_tools(self, tools):
        from langchain_core.tools import StructuredTool
        for t in tools.values():
            assert isinstance(t, StructuredTool)

    def test_descriptions_present(self, tools):
        for t in tools.values():
            assert t.description and len(t.description) > 0

    @pytest.mark.parametrize("tool_name,expected_desc", [
        ("math_double_array", "Doubles every element"),
        ("math_scale_array",  "Multiplies each element"),
        ("math_add_arrays",   "Element-wise addition"),
    ])
    def test_tool_descriptions(self, tools, tool_name, expected_desc):
        assert expected_desc in tools[tool_name].description



class TestArgsSchema:
    def test_double_array_schema_fields(self, tools):
        fields = tools["math_double_array"].args_schema.model_fields
        assert "data" in fields

    def test_scale_array_schema_fields(self, tools):
        fields = tools["math_scale_array"].args_schema.model_fields
        assert "data"   in fields
        assert "scalar" in fields

    def test_add_arrays_schema_fields(self, tools):
        fields = tools["math_add_arrays"].args_schema.model_fields
        assert "a" in fields
        assert "b" in fields

    def test_double_array_data_is_list(self, tools):
        hint = tools["math_double_array"].args_schema.model_fields["data"].annotation
        assert hint == List[float]

    def test_scale_array_scalar_is_float(self, tools):
        hint = tools["math_scale_array"].args_schema.model_fields["scalar"].annotation
        assert hint is float



class TestToolInvocation:
    @pytest.mark.parametrize("inp,expected", [
        ([1.0, 2.0, 3.0], [2.0, 4.0, 6.0]),
        ([5.0],            [10.0]),
        ([1.5, 2.5],       [3.0, 5.0]),
    ])
    def test_double_array(self, tools, inp, expected):
        result = tools["math_double_array"].invoke({"data": inp})
        assert result == expected

    @pytest.mark.parametrize("inp,scalar,expected", [
        ([1.0, 2.0, 3.0], 3.0, [3.0, 6.0, 9.0]),
        ([2.0, 4.0],      5.0, [10.0, 20.0]),
        ([7.0],           6.0, [42.0]),
    ])
    def test_scale_array(self, tools, inp, scalar, expected):
        result = tools["math_scale_array"].invoke({"data": inp, "scalar": scalar})
        assert result == expected

    @pytest.mark.parametrize("a,b,expected", [
        ([1.0, 2.0, 3.0], [4.0, 5.0, 6.0], [5.0, 7.0, 9.0]),
        ([1.0, 2.0],      [0.0, 0.0],       [1.0, 2.0]),
        ([5.0, -3.0],     [-5.0, 3.0],      [0.0, 0.0]),
    ])
    def test_add_arrays(self, tools, a, b, expected):
        result = tools["math_add_arrays"].invoke({"a": a, "b": b})
        assert result == expected



class TestToolErrors:
    def test_mismatched_lengths_raises(self, tools):
        with pytest.raises(Exception):
            tools["math_add_arrays"].invoke({"a": [1.0, 2.0], "b": [1.0]})

    def test_missing_scalar_raises(self, tools):
        with pytest.raises(Exception):
            tools["math_scale_array"].invoke({"data": [1.0]})
