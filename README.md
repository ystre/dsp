# DSP

Reference implementation of Data Stream Processing.

This is an early prototype.

For a detailed documentation see `doc` directory. It is organized into a
AsciiDoc book: `doc/dsp.adoc`.

## Project structure

```
├── cmake
├── deps                    Dependencies (Git submodules)
├── doc
├── include                 Public headers
├── res                     Resources (configurations)
├── unit-tests
├── scripts                 Various scripts, e.g., for testing
├── src
│   ├── dsp                 Data Stream Processing framework
│   ├── svc                 An example service
│   └── tools               Individual executables (like Kafka client)
```
