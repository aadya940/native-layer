"""
native-layer — comprehensive pytest suite.
Run from project root: pytest
"""
import array
import pytest


PLUGIN_NAME = "math"
FUNC_DOUBLE = "double_array"
FUNC_SCALE  = "scale_array"
FUNC_ADD    = "add_arrays"


class TestImport:
    def test_extension_importable(self):
        import native_layer  # noqa: F401

    def test_manager_constructible(self):
        from native_layer import NativeManager
        assert NativeManager() is not None


class TestPluginLoading:
    def test_schema_present(self, manager):
        assert len(manager.get_schema(PLUGIN_NAME)) > 0

    def test_schema_lists_all_functions(self, manager):
        schema = manager.get_schema(PLUGIN_NAME)
        for fn in (FUNC_DOUBLE, FUNC_SCALE, FUNC_ADD):
            assert fn in schema

    def test_missing_dll_raises(self):
        from native_layer import NativeManager
        with pytest.raises(Exception):
            NativeManager().load_plugin("x", "./missing.dll")


class TestF64Buffer:
    @pytest.mark.parametrize("inp,expected", [
        ([1.0, 2.0, 3.0, 4.0], [2.0, 4.0, 6.0, 8.0]),
        ([5.0],                 [10.0]),
        ([1.5, 2.5],            [3.0, 5.0]),
    ])
    def test_double_array(self, manager, inp, expected):
        data = array.array('d', inp)
        assert manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [memoryview(data)]) == expected

    def test_result_is_list(self, manager):
        data = array.array('d', [1.0])
        assert isinstance(manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [memoryview(data)]), list)

    def test_length_preserved(self, manager):
        data = array.array('d', [1.0, 2.0, 3.0])
        assert len(manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [memoryview(data)])) == 3

    def test_large_array(self, manager):
        data = array.array('d', [float(i) for i in range(1000)])
        result = manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [memoryview(data)])
        assert result[0] == 0.0 and result[999] == 1998.0


class TestDtypeDetection:
    def test_f32_buffer_accepted(self, manager):
        """Bridge detects 'f' format — must not segfault regardless of plugin behaviour."""
        data = array.array('f', [1.0, 2.0])
        try:
            manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [memoryview(data)])
        except Exception as exc:
            pytest.fail(f"f32 buffer raised unexpectedly: {exc}")

    def test_empty_f64_buffer(self, manager):
        data = array.array('d', [])
        result = manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [memoryview(data)])
        assert result is None or result == []


class TestScaleArray:
    @pytest.mark.parametrize("inp,scalar,expected", [
        ([1.0, 2.0, 3.0], 3.0,  [3.0, 6.0, 9.0]),
        ([2.0, 4.0],      5,    [10.0, 20.0]),
        ([1.0, 2.0, 3.0], 0.0,  [0.0, 0.0, 0.0]),
        ([1.0, -2.0, 3.0], -1.0, [-1.0, 2.0, -3.0]),
        ([7.0],            6.0,  [42.0]),
    ])
    def test_scale(self, manager, inp, scalar, expected):
        data = array.array('d', inp)
        assert manager.execute(PLUGIN_NAME, FUNC_SCALE, [memoryview(data), scalar]) == expected

    def test_missing_scalar_raises(self, manager):
        data = array.array('d', [1.0])
        with pytest.raises(RuntimeError):
            manager.execute(PLUGIN_NAME, FUNC_SCALE, [memoryview(data)])


class TestAddArrays:
    @pytest.mark.parametrize("a,b,expected", [
        ([1.0, 2.0, 3.0], [4.0, 5.0, 6.0], [5.0, 7.0, 9.0]),
        ([1.0, 2.0],      [0.0, 0.0],       [1.0, 2.0]),
        ([5.0, -3.0],     [-5.0, 3.0],      [0.0, 0.0]),
    ])
    def test_add(self, manager, a, b, expected):
        av = memoryview(array.array('d', a))
        bv = memoryview(array.array('d', b))
        assert manager.execute(PLUGIN_NAME, FUNC_ADD, [av, bv]) == expected

    def test_large_arrays(self, manager):
        a = array.array('d', [float(i) for i in range(500)])
        b = array.array('d', [1.0] * 500)
        result = manager.execute(PLUGIN_NAME, FUNC_ADD, [memoryview(a), memoryview(b)])
        assert result[0] == 1.0 and result[499] == 500.0

    def test_length_mismatch_raises(self, manager):
        with pytest.raises(RuntimeError):
            manager.execute(PLUGIN_NAME, FUNC_ADD,
                            [memoryview(array.array('d', [1.0, 2.0])),
                             memoryview(array.array('d', [1.0]))])

    def test_missing_second_arg_raises(self, manager):
        with pytest.raises(RuntimeError):
            manager.execute(PLUGIN_NAME, FUNC_ADD, [memoryview(array.array('d', [1.0]))])


class TestErrorPaths:
    def test_unknown_function_raises(self, manager):
        with pytest.raises(RuntimeError):
            manager.execute(PLUGIN_NAME, "no_such_fn", [memoryview(array.array('d', [1.0]))])

    def test_unsupported_python_type_raises(self, manager):
        with pytest.raises(Exception):
            manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [{"k": "v"}])

    def test_empty_input_list_raises(self, manager):
        with pytest.raises(RuntimeError):
            manager.execute(PLUGIN_NAME, FUNC_DOUBLE, [])


class TestHelpers:
    @pytest.fixture(autouse=True)
    def _import(self):
        from native_layer.adapters.adk import _to_native, _py_type, _array_typecode
        self.N, self.A, self.T = _to_native, _py_type, _array_typecode

    @pytest.mark.parametrize("schema,code", [
        ({},                        'd'),
        ({'format': 'float32'},     'f'),
        ({'type': 'integer'},       'q'),
    ])
    def test_typecode(self, schema, code):
        assert self.T(schema) == code

    @pytest.mark.parametrize("val,schema,expected", [
        (42,     {'type': 'integer'}, 42),
        (None,   {'type': 'integer'}, 0),
        (3.14,   {'type': 'number'},  3.14),
        ('hi',   {'type': 'string'},  'hi'),
        (None,   {'type': 'string'},  ''),
    ])
    def test_to_native_scalars(self, val, schema, expected):
        result = self.N(val, schema)
        assert result == pytest.approx(expected) if isinstance(expected, float) else result == expected

    def test_to_native_f64_array(self):
        mv = self.N([1.0, 2.0], {'type': 'array', 'items': {'type': 'number'}})
        assert isinstance(mv, memoryview) and mv.format == 'd' and mv.nbytes == 16

    def test_to_native_f32_array(self):
        mv = self.N([1.0], {'type': 'array', 'items': {'type': 'number', 'format': 'float32'}})
        assert mv.format == 'f' and mv.nbytes == 4

    def test_to_native_i64_array(self):
        mv = self.N([10, 20], {'type': 'array', 'items': {'type': 'integer'}})
        assert mv.format == 'q' and mv.nbytes == 16

    @pytest.mark.parametrize("schema,expected", [
        ({'type': 'integer'}, int),
        ({'type': 'number'},  float),
        ({'type': 'string'},  str),
    ])
    def test_annotation_scalars(self, schema, expected):
        assert self.A(schema) is expected

    def test_annotation_float_array(self):
        from typing import List
        assert self.A({'type': 'array', 'items': {'type': 'number'}}) == List[float]

    def test_annotation_int_array(self):
        from typing import List
        assert self.A({'type': 'array', 'items': {'type': 'integer'}}) == List[int]


class TestPydanticModelBuilder:
    @pytest.fixture(autouse=True)
    def _mw(self):
        from native_layer.adapters.langchain import NativeHotReloadMiddleware
        self.build = NativeHotReloadMiddleware.__new__(NativeHotReloadMiddleware)._build_pydantic_model

    @pytest.mark.parametrize("props,keys", [
        ({"x": {"type": "number"}},                          {"x"}),
        ({"n": {"type": "integer"}},                         {"n"}),
        ({"s": {"type": "string"}},                          {"s"}),
        ({"d": {"type": "array", "items": {"type": "number"}}}, {"d"}),
        ({"a": {"type": "number"}, "b": {"type": "integer"}}, {"a", "b"}),
        ({},                                                  set()),
    ])
    def test_model_fields(self, props, keys):
        m = self.build("t", {"properties": props})
        assert set(m.model_fields.keys()) == keys
