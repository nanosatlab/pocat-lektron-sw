# POCAT Lektron SW - Testing Infrastructure

This directory contains the **Host-Based Unit Testing** infrastructure for the flight software. 

The goal of this architecture is to allow the logical validation of the control code (circular queues, state machines, etc.) by compiling and executing the code natively on the developer's computer (or inside a Docker container), isolating it completely from the physical hardware (STM32) and the RTOS (FreeRTOS).

---

## Architecture Philosophy: The Dual Workflow

When developing embedded software for space, there are two distinct types of testing that must coexist:

1. **Hardware Verification (STM32):** You compile the code using the `arm-none-eabi-gcc` cross-compiler and flash it to the physical STM32 board. This proves that electrical signals (SPI, I2C) and peripherals work. 
2. **Logic Verification (Host-Based / Docker):** You extract the the logic and compile it using a native C compiler on your computer. This allows you to run hundreds of tests in milliseconds and easily simulate impossible hardware failures to verify that your mathematics and algorithms are robust.

This directory (`test/`) is **strictly ignored** when compiling the flight software for the STM32. It only activates when the CMake flag `-DBUILD_TESTING=ON` is provided.
---

##  Directory Structure

The testing sandbox is divided into four main modular folders:

- **`framework/`**: Contains the testing engine. We use **Unity** (by ThrowTheSwitch), an industry-standard, lightweight C testing framework. 
- **`support/`**: Contains auxiliary tools and physical simulators. For example, `fake_flash_memory.c` creates a large RAM array on the computer that physically mimics the STM32 Flash memory, allowing tests to inspect exactly what bytes the code attempted to save.
- **`mocks/`**: Our flight code tries to talk to FreeRTOS and the STM32 HAL. Since the computer lacks these, we provide fake headers (`stubs_hal/`, `stubs_rtos/`) and fake functions (`mock_obdh_requests.c`). The flight code is tricked into thinking it is running in space, while actually interacting with our controlled mocks. Mocks also allow us to inject errors to observe how the flight code reacts.
- **`suites/`**: The actual test scripts written by developers. These files configure the mocks, execute the real flight functions, and assert that the outcomes are correct.

---

## How to Run the Tests

To ensure that the tests compile flawlessly on any machine (Windows, Mac, Linux, or a CI/CD pipeline) without needing to install C compilers or CMake locally, the execution is wrapped in a **Docker container**.

### Prerequisites
Make sure **Docker Desktop** is open and running in the background.

### Execution
Open a terminal at the **root** of the repository (`pocat-lektron-sw/`) and run:

1. **Build the testing container (Only needed once or if Dockerfile changes):**
   ```bash
   docker build -t pocat-tests -f Dockerfile.test .
   ```

2. **Execute the tests:**
   ```bash
   docker run --rm -v ${PWD}:/app pocat-tests
   ```


