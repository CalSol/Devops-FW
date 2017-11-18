# Tachyon
Microcontroller code for Tachyon

## TODOs
- Update to latest mbed
- Split common libraries into separate submodule

## Quick Links
- [Tips for updating the mbed submodule](UPDATING-MBED.md)

## Setup
### Toolchain installation
0. Install [GCC-ARM](https://launchpad.net/gcc-arm-embedded) to build the embedded firmware.
  - On Debian-based systems (including Ubuntu), a [PPA and the instructions for setting it up are available](https://launchpad.net/~team-gcc-arm-embedded/+archive/ubuntu/ppa). Note: the one included by default in the system package manager may not work.
    - If you cannot run the `add-apt-repository` command, you may need to install `software-properties-common`:

      ```
      sudo apt-get install software-properties-common
      ```

  - On Windows and Mac, [download and run this installer](https://launchpad.net/gcc-arm-embedded).
    - On Windows, at the end of the installation, make sure to check "Add path to environment variable" so it can be run from anywhere.
0. Install [SCons](http://scons.org/), the build system.
  - On Debian-based systems, this is available as a package:

    ```
    sudo apt-get install scons
    ```

  - On Windows, [download and run this installer](http://scons.org/pages/download.html). You will need to install [Python 2.7, 32-bit](https://www.python.org/downloads/) if you do not have it already (as of SCons 2.5.0, there is no Python3 support yet and the installer will not detect 64-bit Python versions).
    - Make sure to add SCons to your system PATH. By default, SCons will be installed in the `Scripts` folder under your Python directory.
0. Install [OpenOCD](http://openocd.org/), a program that interfaces with the debug link.
  - On Debian-based systems, this is available as a package:

    ```
    sudo apt-get install openocd
    ```

  - On Windows, an unofficial installer is available [here](https://github.com/gnuarmeclipse/openocd/releases). There are no official binaries without requiring additional environments like MSYS.
  - If you would like to run OpenOCD from the command line, make sure to set the proper environment variables:
    - On Windows, add the OpenOCD binary directory to your system `PATH`. The default is `C:\Program Files\GNU ARM Eclipse\OpenOCD\(version)\bin`.
    - Add the OpenOCD scripts directory to your system `OPENOCD_SCRIPTS`, this allows you to run OpenOCD from anywhere without needing to explicitly specify the scripts directory location:
      - On Debian-based systems, this is `/usr/share/openocd/scripts`.
      - On Windows, the default is `C:\Program Files\GNU ARM Eclipse\OpenOCD\(version)\scripts`
      - This folder should contain the file `interface/cmsis-dap.cfg`.
  - On Windows 8 and later, the [CDC Driver](https://github.com/x893/CMSIS-DAP/blob/master/Firmware/STM32/CMSIS_DAP.inf) for the CMSIS-DAP dongles may solve an issue with the dongles becoming non-responsive after a few seconds.

###Toolchain Installation for Mac:
  - Download and install homebrew 
  - Enter the following commands in terminal: 
  ```
  brew tap PX4/homebrew-px4
  brew install gcc-arm-none-eabi

  brew install scons 
  
  brew install openocd
  ```
  
## Repository Checkout
_Note:_ This repository uses Git submodules as a way to bring in external dependencies (like the mbed library) while both tracking the version used and allowing easier updates. Other than the initial clone and updates described below, it is unlikely you will need to significantly interact with the submodule system for routine development. However, there is plenty of documentation on submodules available on the Internet for those who wish to know more.

### GUI option
_GitHub Desktop is only available for Windows and Mac. GitHub Desktop handles most submodule operations for you, cloning submoduled repositories and updating submodule pointers during a sync operation._

0. Download and install [GitHub Desktop](https://desktop.github.com/).
0. Clone this repository to desktop using the "Clone or download" button on the web interface. It should automatically launch GitHub Desktop.
0. In the GitHub Desktop interface, you can sync the repository (push new changes to GitHub if you have the appropriate permissions, as well as pull updates from GitHub) using the "Sync" button.

### Command-line option
0. Ensure command-line git is installed.
  - On Debian-based systems (including Ubuntu), this is available as a package.

    ```
    sudo apt-get install git
    ```

  - On Windows, [download and run this installer](https://git-scm.com/download/win).
    - At the end of the installation, check "Use Git from the Windows Command Prompt" if you want to run Git from outside the Git Bash command prompt.
0. Clone (download a copy of) the repository:

  ```
  git clone --recursive https://github.com/CalSol/Tachyon-FW.git
  ```

0. To pull new remote updates into your local repository from GitHub:

  ```
  git pull
  git submodule update --init --recursive
  ```

  - Note that `git pull` does not update your submodules' working directory. The subsequent `git submodule update` command does that, and the `--init` and `--recursive` options ensure new submodules are cloned and submodules-within-submodules are handled.
  - When doing commits, if it indicates that a submodule directory changed that you didn't actually change, you may have forgotten to do a `git submodule update`. Git will see that the (outdated) working directory submodule is pointing to a different commit than the one in HEAD, and interpret that as a change.
0. Using `git` effectively has a learning curve, but as Git is everywhere now, it's worth learning. Make sure you're familiar with Git commands like `commit`, `pull`, `push`, `merge`, `rebase`, and `cherry-pick`.

## Development
### Installing Eclipse
_This section is optional, for people who want to work with an IDE and GUI debug tools. This is recommended for beginners, as it provides a low learning curve and integrated way to code, program, and debug firmware. For the masochists among us that love vim/emacs and command-line GDB, see the [command-line operations section](#command-line-operations)._

#### Initial setup
0. [Download Eclipse](https://www.eclipse.org/downloads/). Eclipse IDE for C/C++ developers is a good option.
0. Install some Eclipse plugins:
  - (menu) > Help > Install New Software..., then enter the update site URL in the "Work with..." field.
  - If some components cannot be installed, you may need to update your version of Eclipse.
  - Install [SConsolidator](http://www.sconsolidator.com/) (update site: <http://www.sconsolidator.com/update>), which allows some integration of SCons scripts.
  - Install [GNU ARM on Eclipse](https://gnuarmeclipse.github.io) (update site: <http://gnuarmeclipse.sourceforge.net/updates>), which provides Eclipse program and debug integration.
0. Define where the OpenOCD binary can be found:
  - (menu) > Preferences > Run/Debug > OpenOCD, set the Folder to the folder where the OpenOCD binary is located.

#### Project configuration
0. Add this repository as a Eclipse project:
  - (menu) > New > Project > New SCons project from existing source. Under Existing Code Location, choose the folder where you checked out this repository.
0. Configure the indexer to include the proper system headers for this project:
  - Right click the project and open the Properties window, then under C/C++ General > Paths and Symbols, in the Includes tab, for both GNU C and GNU C++ languages, add the GNU ARM includes directory:
    - Under Debian-based systems, the path is `/usr/lib/gcc/arm-none-eabi/(version)/include`.
    - Under Windows, the default is `C:\Program Files (x86)\GNU Tools ARM Embedded\(version)\arm-none-eabi\include`.
0. Build the project.
  - In the Project Explorer, right-click the Tachyon-FW project and click Build.
  - If the build succeeded, this should create all the `.elf` files (compiled firmware) in the `build/` directory.
  - PROTIP: you can also start a build with Ctrl+B.
0. Set up a debug configuration. Right-click the `.elf` file and select Debug As > Debug Configurations...
  0. From the list on the left side of the new window, right click GDB OpenOCD Debugging and select New.
  0. Under the Main tab:
    - Give the configuration a name.
    - Ensure the Project field matches the project name.
    - Set the C/C++ Application to the path to the `.elf` file, like `build/oled2.elf`.
    - If you want to auto-build before launching, set it in Build (if required) before launching, either for this debug target only, or by modifying the workspace-wide setting.
  0. Under the Debugging tab:
    - Leave the default OpenOCD Setup > Executable configuration of `${openocd_path}/${openocd_executable}`.
    - Set the GDB Client Setup > Executable to `arm-none-eabi-gdb`.
    - Set the Config Options to include flags for interface / target configuration (`-f`), and startup commands (`-c`):

      ```
      -f interface/cmsis-dap.cfg
      -f target/<your-target-config.cfg>
      -c init
      -c "reset halt"
      ```

      For target config files, see [OpenOCD Target Configurations](#openocd-target-configurations).

  0. Under the Startup tab:
    - In Run/Restart Commands, if you want the target to start running immediately after flashing completes, disable the Set breakpoint at: option.
  0. Under the Common tab:
    - If you want to launch this target from the quickbar, check the options in Display in favorites menu.
0. Try launching the debug target. This will flash the microcontroller and start it.
  - If all goes well, you should be able to pause the target (and Eclipse should bring up the next line of code the microcontroller will execute). The normal debugging tools are available: step-into, step-over, step-out, breakpoints, register view, memory view, and more.
  - PROTIP: you can launch the last debug target with Ctrl+F11.
  - Make sure to terminate a debugging session when done. Eclipse does not allow multiple concurrent debug sessions.

### Command-Line Operations
_If your natural habitat is in front of a text terminal rather than a GUI, you can also flash and debug the microcontroller using command-line OpenOCD._
#### Building with SCons
0. Invoke SCons in the `Tachyon-FW` folder to build all the targets.

  ```
  scons
  ```

  This places the build binaries (.elf and .bin files) in `build/`. SCons has built-in dependency tracking so it does a minimal incremental build.
  - `scons -c` will clean all built targets. Or, if you prefer to be sure by nuking it from orbit, delete the `build/` directory.
  - You can build specific files (or directories) with `scons <your/target/here>`.

#### Flashing with OpenOCD and CMSIS-DAP dongles
0. Ensure the built binaries (`.bin` files) are up to date by invoking `scons`.
0. Do a sanity check by launching OpenOCD with the interface and target configuration:

  ```
  openocd \
    -f interface/cmsis-dap.cfg \
    -f <your-target-config.cfg>
  ```

  For target config files, see [OpenOCD Target Configurations](#openocd-target-configurations).

  - If all worked well, you should see something ending with:

    ```
    Info : CMSIS-DAP: Interface ready
    Info : clock speed 4000 kHz
    Info : SWD IDCODE 0x2ba01477
    Info : lpc1549.cpu: hardware has 6 breakpoints, 4 watchpoints
    ```

    Obviously, this may vary depending on the particular chip target. At this point, OpenOCD continues running (as a server) in the foreground, waiting for interactive commands from socket connections.
  - If the CMSIS-DAP dongle isn't connected to your computer, you would see an error like:

    ```
    Error: unable to find CMSIS-DAP device
    ```

  - If the target board isn't connected, you might see an error like:

    ```
    Info : clock speed 480 kHz
    in procedure 'init'
    in procedure 'ocd_bouncer'
    ```

0. Without closing the running OpenOCD server, open a telnet connection to localhost:4444, the OpenOCD console.
  0. In the OpenOCD console, run these commands:

    ```
    reset halt
    flash erase_sector 0 0 last
    flash write_image <path/to/your/image.bin>
    ```

    Note that the path to the image is relatively to where the OpenOCD server is running, not where your telnet client is running.

  0. When you're ready to start your target, run this in the OpenOCD console:

    ```
    reset
    ```

  0. When you're done, you can stop the OpenOCD server with:

    ```
    exit
    ```

  0. This is only a small subset of commands available through OpenOCD. Those interested should read the [commands section of the official OpenOCD documentation](http://openocd.org/doc/html/General-Commands.html).

0. Instead of running commands interactively, you can also automate the above by passing in commands to run through OpenOCD's arguments:

  ```
  openocd \
    -f interface/cmsis-dap.cfg \
    -f <your-target-config.cfg> \
    -c init \
    -c "reset halt" \
    -c "flash erase_sector 0 0 last" \
    -c "flash write_image <path/to/your/image.bin>" \
    -c "reset run" \
    -c "exit"
  ```

  - Note that this immediately starts your program on the target after flashing; if you don't want this behavior, remove the `-c "reset run"` line.

0. OpenOCD integrates with command-line GDB, if that's your thing. Compared to a modern IDE, GDB has a steeper learning curve, so how to use GDB is outside the scope of this lab. However, [instructions for integrating OpenOCD and GDB are available](http://openocd.org/doc/html/GDB-and-OpenOCD.html).

### OpenOCD Target Configurations
Examples of the target config are:
- `lpc1549_openocd.cfg` (in this repository) for LPC1549 targets.
