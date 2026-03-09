import os
import asyncio
import time
import shutil
from dotenv import load_dotenv

from google.adk.agents.llm_agent import Agent
from google.adk.models.google_llm import Gemini
from google.adk.runners import Runner
from google.adk.sessions import InMemorySessionService
from google.genai import types

from native_layer import NativeManager
from native_layer.adapters.adk import NativeADKToolset

load_dotenv(".env")

async def main():
    manager = NativeManager()
    plugins_dir = os.path.abspath("./plugins")
    if not os.path.exists(plugins_dir):
        os.makedirs(plugins_dir)
    
    manager.watch_directory(plugins_dir)
    print("Native Manager initialized. Monitoring /plugins directory.")

    shutil.copyfile("./plugin_simsimd.dll", "./plugins/plugin_simsimd.dll")

    # Synchronize C++ Toolset
    print("Synchronizing C++ Toolset. Verifying plugin_simsimd.dll...")

    # Give the manager a moment to process the copy.
    await asyncio.sleep(0.2)
    
    active_tools = manager.get_active_tools()
    if not active_tools:
        print("Error: No C++ plugins detected. Verify plugin_simsimd.dll is in /plugins.")
        return
    
    print(f"Plugins synchronized: {active_tools}")

    # ADK Configuration
    APP_NAME = "simd_compute_app"
    model = Gemini(model_id="gemini-3-flash-preview")
    native_toolset = NativeADKToolset(manager)
    session_service = InMemorySessionService()

    root_agent = Agent(
        model=model,
        name="native_systems_agent",
        description="High-performance agent utilizing C++ SIMD plugins.",
        instruction="Use native C++ tools for vector calculations to ensure maximum performance.",
        tools=[native_toolset],
    )

    runner = Runner(
        agent=root_agent, 
        app_name=APP_NAME, 
        session_service=session_service
    )

    # Session Management
    user_id = "dev"
    session_id = f"session_{int(time.time())}"
    await session_service.create_session(
        app_name=APP_NAME, 
        user_id=user_id, 
        session_id=session_id
    )
    
    user_query = "What is the similarity between [1, 2, 3] and [1, 0, 3]?"
    message = types.Content(
        role="user", 
        parts=[types.Part(text=user_query)]
    )

    print(f"User Query: {user_query}")

    # Execution Loop
    try:
        async for event in runner.run_async(
            user_id=user_id, 
            session_id=session_id, 
            new_message=message
        ):
            if event.content and event.content.parts:
                for part in event.content.parts:
                    if part.text:
                        print(part.text, end="", flush=True)
            
            if event.get_function_calls():
                for call in event.get_function_calls():
                    print(f"\n[Executing Native Call: {call.name}]")

    except Exception as e:
        print(f"Execution Error: {e}")
    finally:
        await native_toolset.close()

if __name__ == "__main__":
    asyncio.run(main())
