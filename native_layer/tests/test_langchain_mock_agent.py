"""
LangChain mock-model agent loop tests.

Uses a hand-built FakeToolCallingChatModel (no live LLM) to verify:
  - The middleware injects tools into agent state
  - The model sees tool schemas via bind_tools
  - A canned tool-call response triggers real native C++ execution
  - Results feed back into the message sequence correctly

Run from project root: pytest
"""
import pytest
from typing import Any, Iterator, List, Optional, Sequence

from langchain_core.language_models import BaseChatModel
from langchain_core.messages import AIMessage, BaseMessage, ToolCall, ToolMessage
from langchain_core.outputs import ChatGeneration, ChatResult
from langchain_core.tools import BaseTool
from langchain_core.runnables import RunnableConfig


# Minimal fake chat model

class FakeToolCallingChatModel(BaseChatModel):
    """
    Returns pre-programmed AIMessage responses in order.
    Supports bind_tools so the agent framework can attach tool schemas.
    """
    responses: List[AIMessage]
    _call_index: int = 0

    class Config:
        arbitrary_types_allowed = True

    def bind_tools(self, tools: Sequence[BaseTool], **kwargs):
        return self  # tools are noted by the agent framework; we don't need them

    def _generate(
        self,
        messages: List[BaseMessage],
        stop: Optional[List[str]] = None,
        run_manager: Any = None,
        **kwargs,
    ) -> ChatResult:
        idx = self._call_index
        self._call_index += 1
        msg = self.responses[idx] if idx < len(self.responses) else AIMessage(content="done")
        return ChatResult(generations=[ChatGeneration(message=msg)])

    @property
    def _llm_type(self) -> str:
        return "fake-tool-calling"


# Fixtures

@pytest.fixture(scope="module")
def middleware(manager):
    from native_layer.adapters.langchain import NativeHotReloadMiddleware
    return NativeHotReloadMiddleware(manager)

@pytest.fixture(scope="module")
def tools(middleware):
    return {t.name: t for t in middleware._get_live_tools()}



class TestMiddlewareInjection:
    def test_wrap_model_call_injects_tools(self, middleware, tools):
        """wrap_model_call must populate request.tools before calling the handler."""
        from langchain.agents.middleware import ModelRequest, ModelResponse

        fake_model = FakeToolCallingChatModel(responses=[AIMessage(content="ok")])
        captured = {}

        def fake_handler(req: ModelRequest) -> ModelResponse:
            captured["tools"] = list(req.tools)
            return ModelResponse(result=[AIMessage(content="ok")])

        req = ModelRequest(model=fake_model, messages=[], tools=[])
        middleware.wrap_model_call(req, fake_handler)

        assert len(captured["tools"]) > 0

    def test_injected_tools_include_all_functions(self, middleware):
        from langchain.agents.middleware import ModelRequest, ModelResponse

        fake_model = FakeToolCallingChatModel(responses=[AIMessage(content="ok")])
        captured = {}

        def fake_handler(req):  
            captured["names"] = [t.name for t in req.tools]
            return ModelResponse(result=[AIMessage(content="ok")])

        middleware.wrap_model_call(ModelRequest(model=fake_model, messages=[], tools=[]), fake_handler)

        assert "math_double_array" in captured["names"]
        assert "math_scale_array"  in captured["names"]
        assert "math_add_arrays"   in captured["names"]



class TestBindToolsExecution:
    """
    Simulate one LLM turn: model returns a tool_call, we execute it manually
    (the same way an agent loop would), verify real native C++ is called.
    """

    def _make_model(self, tool_name: str, args: dict) -> FakeToolCallingChatModel:
        return FakeToolCallingChatModel(responses=[
            AIMessage(content="", tool_calls=[
                ToolCall(id="call_001", name=tool_name, args=args)
            ]),
            AIMessage(content="done"),
        ])

    def test_double_array_tool_call(self, tools):
        model = self._make_model("math_double_array", {"data": [1.0, 2.0, 3.0]})
        model_with_tools = model.bind_tools(list(tools.values()))

        from langchain_core.messages import HumanMessage
        response = model_with_tools.invoke([HumanMessage(content="double [1,2,3]")])

        assert len(response.tool_calls) == 1
        tc = response.tool_calls[0]
        assert tc["name"] == "math_double_array"

        result = tools[tc["name"]].invoke(tc["args"])
        assert result == [2.0, 4.0, 6.0]

    def test_scale_array_tool_call(self, tools):
        model = self._make_model("math_scale_array", {"data": [1.0, 2.0], "scalar": 5.0})
        model_with_tools = model.bind_tools(list(tools.values()))

        from langchain_core.messages import HumanMessage
        response = model_with_tools.invoke([HumanMessage(content="scale it")])

        tc = response.tool_calls[0]
        result = tools[tc["name"]].invoke(tc["args"])
        assert result == [5.0, 10.0]

    def test_add_arrays_tool_call(self, tools):
        model = self._make_model("math_add_arrays", {"a": [1.0, 2.0], "b": [3.0, 4.0]})
        model_with_tools = model.bind_tools(list(tools.values()))

        from langchain_core.messages import HumanMessage
        response = model_with_tools.invoke([HumanMessage(content="add them")])

        tc = response.tool_calls[0]
        result = tools[tc["name"]].invoke(tc["args"])
        assert result == [4.0, 6.0]



class TestCreateAgentLoop:
    """
    Run a complete two-turn loop through create_agent:
      Turn 1: fake model returns a tool call
      Turn 2: fake model returns final answer
    Verifies that native C++ code is invoked during the loop.
    """

    def test_agent_executes_tool_and_completes(self, tools):
        from langchain.agents import create_agent
        from langchain_core.messages import HumanMessage

        model = FakeToolCallingChatModel(responses=[
            AIMessage(content="", tool_calls=[
                ToolCall(id="c1", name="math_double_array", args={"data": [2.0, 4.0]})
            ]),
            AIMessage(content="The result is [4.0, 8.0]."),
        ])

        agent = create_agent(model, list(tools.values()))
        result = agent.invoke({"messages": [HumanMessage(content="double [2, 4]")]})

        messages = result["messages"]
        # Should have: human, ai (tool_call), tool result, ai (final)
        assert len(messages) >= 3

        # Find the ToolMessage (native execution result)
        tool_msgs = [m for m in messages if isinstance(m, ToolMessage)]
        assert len(tool_msgs) == 1

        import ast
        executed_result = ast.literal_eval(tool_msgs[0].content)
        assert executed_result == [4.0, 8.0]

    def test_agent_final_message_is_ai(self, tools):
        from langchain.agents import create_agent
        from langchain_core.messages import HumanMessage

        model = FakeToolCallingChatModel(responses=[
            AIMessage(content="", tool_calls=[
                ToolCall(id="c2", name="math_scale_array", args={"data": [3.0], "scalar": 2.0})
            ]),
            AIMessage(content="The scaled result is [6.0]."),
        ])

        agent = create_agent(model, list(tools.values()))
        result = agent.invoke({"messages": [HumanMessage(content="scale it")]})

        last = result["messages"][-1]
        assert isinstance(last, AIMessage)
        assert "6.0" in last.content
