# LTX/1 Compatibility Fixtures

These fixtures are canonical byte-level samples for independent implementations.

Generate the memory HELLO fixture with:

```powershell
.\build\release\lattice.exe fixture --memory-hello
```

Validate it with:

```powershell
.\build\release\lattice.exe replay fixtures\ltx1\memory_hello.trace
```
