# Tachyon
Microcontroller code for Tachyon

## Setup
### Toolchain installation
You will install these components of the build system here:
- [GCC-ARM](https://launchpad.net/gcc-arm-embedded), the compiler targeting the embedded microcontroller.
- [SCons](http://scons.org/), the build system.
- [OpenOCD](http://openocd.org/), a program that interfaces with the debug link. 

Platform-specific directions are below.

#### For Debian-based Linux
1.  Install the [GCC-ARM PPA by following these instructions](https://launchpad.net/~team-gcc-arm-embedded/+archive/ubuntu/ppa). Note: the one included by default in the system package manager may not work.
    - If you cannot run the `add-apt-repository` command, you may need to install `software-properties-common`:
      ```
      sudo apt-get install software-properties-common
      ```
1.  Install everything else:
    ```
    sudo apt-get install git
    sudo apt-get install scons
    sudo apt-get install openocd
    ```
1.  Add the OpenOCD scripts directory to your system `OPENOCD_SCRIPTS`, allowing you to run OpenOCD from anywhere without needing to explicitly specify the scripts directory location. This is typically in `/usr/share/openocd/scripts`, and should contain the file `interface/cmsis-dap.cfg`.
    
#### For Windows
1.  [Install command-line git](https://git-scm.com/download/win).
      - At the end of the installation, check "Use Git from the Windows Command Prompt" if you want to run Git from outside the Git Bash command prompt.
      - Whether you use command-line git, GitHub desktop, or some other interface (more details below), this is still required for the build system to sanity check submodule status.
1.  [Install GCC-ARM](https://developer.arm.com/open-source/gnu-toolchain/gnu-rm/downloads).
    - At the end of the installation, make sure to check "Add path to environment variable" so it can be run from anywhere.
1.  Install SCons using pip. On the command line, run:

    ```
    pip install scons
    ```

    - `pip` should be included with your Python install. [Download and install Python](https://www.python.org/downloads/) if you do not have it already. SCons3 is compatible with either Python2 or 3.
    - If you get the error `option --single-version-externally-managed not recognized`, you need to install or upgrade some Python packages. On the command line, run:

      ```
      pip install -U setuptools
      pip install -U wheel
      ```

1.  [Install OpenOCD v0.10.0-5-20171110 using the unofficial .exe installer](https://github.com/gnu-mcu-eclipse/openocd/releases/tag/v0.10.0-5-20171110). There are no official binaries without requiring additional environments like MSYS.
    - If you want to run OpenOCD from the command line, add the OpenOCD binary directory to your system `PATH`. The default is `C:\Program Files\GNU ARM Eclipse\OpenOCD\(version)\bin`.
1.  Add the OpenOCD scripts directory to your system `OPENOCD_SCRIPTS`, allowing you to run OpenOCD from anywhere without needing to explicitly specify the scripts directory location. This is typically in `C:\Program Files\GNU ARM Eclipse\OpenOCD\(version)\scripts`, and should contain the file `interface/cmsis-dap.cfg`.
    - On Windows 10, you can add environment variables by going to the start ment, then "Edit the system environment variables", which brings up the System Properties dialog. Click the "Environment Variables..." button to bring up the Environment Variables dialog. Under "System variables", either edit or add (with "New...") `OPENOCD_SCRIPTS`.
1.  On Windows 8 and later, the [CDC Driver](https://github.com/x893/CMSIS-DAP/blob/master/Firmware/STM32/CMSIS_DAP.inf) for the CMSIS-DAP dongles may solve an issue with the dongles becoming non-responsive after a few seconds.

#### For Mac
1.  Download and install homebrew .
1.  Enter the following commands in terminal: 
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

**GitHub Desktop provides a graphical user interface to most common git functionality, and is recommended if you're new to coding.**

1. Download and install [GitHub Desktop](https://desktop.github.com/).
1. Clone this repository to desktop using the "Clone or download" button on the web interface. It should automatically launch GitHub Desktop.
1. In the GitHub Desktop interface, you can sync the repository (push new changes to GitHub if you have the appropriate permissions, as well as pull updates from GitHub) using the "Sync" button.

### Command-line option
_Command-line git is more powerful but also has a steep learning curve._

**You'll be using git in nearly all CS/EE classes. Although it's harder to learn, you're going to have to eventually :)**

1.  Command-line git should have been installed during the toolchain setup above.
1.  Clone (download a copy of) the repository:

    ```
    git clone --recursive https://github.com/CalSol/Tachyon-FW.git
    ```

1.  To pull new remote updates into your local repository from GitHub:

    ```
    git pull
    git submodule update --init --recursive
    ```

    - Note that `git pull` does not update your submodules' working directory. The subsequent `git submodule update` command does that, and the `--init` and `--recursive` options ensure new submodules are cloned and submodules-within-submodules are handled.
    - When doing commits, if it indicates that a submodule directory changed that you didn't actually change, you may have forgotten to do a `git submodule update`. Git will see that the (outdated) working directory submodule is pointing to a different commit than the one in HEAD, and interpret that as a change.
1.  Using `git` effectively has a learning curve, but as Git is everywhere now, it's worth learning. Make sure you're familiar with Git commands like `commit`, `pull`, `push`, `merge`, `rebase`, and `cherry-pick`.

## Development
_There are two options for code development: using an IDE or a text editor and command line. 
- An IDE (Eclispe, IntelliJ, PyCharm, VSCode, NetBeans) gives an integrated way to code, program, and debug firmware. It requires more setup, but once done it's convinient and very powerful. 
- Using a text editor (Sublime, Atom, vim, emacs, Notepad++) and command line is a more bare-bones way to code. There's no additional setup, but it's a little harder to learn. See the [command-line operations section](#command-line-operations)._

### IDE Setup
_This section is optional, for people who want to work with an IDE and GUI debug tools._

#### Installing Eclipse and add-ons 
1.  [Download Eclipse](https://www.eclipse.org/downloads/). Eclipse IDE for C/C++ developers is a good option.
1.  Install some Eclipse plugins:
    - (menu) > Help > Install New Software..., then enter the update site URL in the "Work with..." field.
    - If some components cannot be installed, you may need to update your version of Eclipse.
    1. Install [SConsolidator](http://www.sconsolidator.com/) (update site: <http://www.sconsolidator.com/update>), which allows some integration of SCons scripts.
    1. Install [GNU ARM on Eclipse](https://gnuarmeclipse.github.io) (update site: <http://gnuarmeclipse.sourceforge.net/updates>), which provides Eclipse program and debug integration.
1.  Define where the OpenOCD binary can be found:
    - (menu) > Preferences > Run/Debug > OpenOCD, set the Folder to the folder where the OpenOCD binary is located.

#### Project configuration
1.  Add this repository as a Eclipse project:
    - (menu) > New > Project > New SCons project from existing source. Under Existing Code Location, choose the folder where you checked out this repository.
1.  Configure the indexer to include the proper system headers for this project:
    - Right click the project and open the Properties window, then under C/C++ General > Paths and Symbols, in the Includes tab, for both GNU C and GNU C++ languages, add the GNU ARM includes directory:
      - Under Debian-based systems, the path is `/usr/lib/gcc/arm-none-eabi/(version)/include`.
      - Under Windows, the default is `C:\Program Files (x86)\GNU Tools ARM Embedded\(version)\arm-none-eabi\include`.
1.  Build the project.
    - In the Project Explorer, right-click the Tachyon-FW project and click Build.
    - If the build succeeded, this should create all the `.elf` files (compiled firmware) in the `build/` directory.
    - PROTIP: you can also start a build with Ctrl+B.
1.  Set up a debug configuration. Right-click the `.elf` file and select Debug As > Debug Configurations...
    1.  From the list on the left side of the new window, right click GDB OpenOCD Debugging and select New.
    1.  Under the Main tab:
        - Give the configuration a name.
        - Ensure the Project field matches the project name.
        - Set the C/C++ Application to the path to the `.elf` file, like `build/oled2.elf`.
        - If you want to auto-build before launching, set it in Build (if required) before launching, either for this debug target only, or by modifying the workspace-wide setting.
    1.  Under the Debugging tab:
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

    1.  Under the Startup tab:
        - In Run/Restart Commands, if you want the target to start running immediately after flashing completes, disable the Set breakpoint at: option.
    1.  Under the Common tab:
        - If you want to launch this target from the quickbar, check the options in Display in favorites menu.
1.  Try launching the debug target. This will flash the microcontroller and start it.
    - If all goes well, you should be able to pause the target (and Eclipse should bring up the next line of code the microcontroller will execute). The normal debugging tools are available: step-into, step-over, step-out, breakpoints, register view, memory view, and more.
    - PROTIP: you can launch the last debug target with Ctrl+F11.
    - Make sure to terminate a debugging session when done. Eclipse does not allow multiple concurrent debug sessions.

### Command-Line Operations
_You can also flash and debug the microcontroller using command-line OpenOCD._
#### Building with SCons
1.  Invoke SCons in the `Tachyon-FW` folder to build all the targets.

    ```
    scons
    ```

    This places the build binaries (.elf and .bin files) in `build/`. SCons has built-in dependency tracking so it does a minimal incremental build.
    - `scons -c` will clean all built targets. Or, if you prefer to be sure by nuking it from orbit, delete the `build/` directory.
    - You can build specific files (or directories) with `scons <your/target/here>`.

#### Flashing with OpenOCD and CMSIS-DAP dongles via SCons
_SCons has build targets that invoke OpenOCD to flash hardware. These will automatically (re)compile all dependencies as needed._

1.  You can see a list of all top-level build targets using:

    ```
    scons --list
    ```
    
    This includes both the binary targets (like `datalogger`) as well as the flash targets (like `flash-datalogger`)
1.  "Build" the flash target:

    ```
    scons flash-<target>
    ```
    
    (replacing `target` with the name of target - so you'd run something like `scons flash-datalogger`)
    - If all worked well, you should see something ending with:
    
      ```
      ** Programming Started **
      auto erase enabled
      Warn : Verification will fail since checksum in image (0x00000000) to be written to flash is different from calculated vector checksum (0xfdff47b6).
      Warn : To remove this warning modify build tools on developer PC to inject correct LPC vector checksum.
      wrote 8192 bytes from file build/datalogger.elf in 1.080895s (7.401 KiB/s)
      ** Programming Finished **
      scons: done building targets.
      ```
      
    - If the CMSIS-DAP dongle isn't connected to your computer, you would see an error like:

      ```
      Error: unable to find CMSIS-DAP device
      Error: No Valid JTAG Interface Configured.
      scons: *** [build\flash-datalogger] Error -1
      scons: building terminated because of errors.
      ```

    - If the target board isn't connected, you might see an error like:

      ```
      in procedure 'program'
      in procedure 'init' called at file "embedded:startup.tcl", line 495
      in procedure 'ocd_bouncer'
      ** OpenOCD init failed **
      shutdown command invoked
      
      scons: *** [build\flash-datalogger] Error -1
      scons: building terminated because of errors.
      ```
    
#### Flashing with OpenOCD and CMSIS-DAP dongles
_This section is keps as a reference only, as to what the SCons flash targets do under the hood. You should use the SCons flash targets from the above section._

1.  Ensure the built binaries (`.bin` files) are up to date by invoking `scons`.
1.  Do a sanity check by launching OpenOCD with the interface and target configuration:

    ```
    openocd \
      -f interface/cmsis-dap.cfg \
      -f <your-target-config.cfg>
    ```

    Reaplce `<your-target-config.cfg>` with the appropriate target file. See [OpenOCD Target Configurations](#openocd-target-configurations).

    - If all worked well, you should see something ending with:

      ```
      Info : CMSIS-DAP: Interface ready
      Info : clock speed 4000 kHz
      Info : SWD IDCODE 0x2ba01477
      Info : lpc1549.cpu: hardware has 6 breakpoints, 4 watchpoints
      ```

      Obviously, this may vary depending on the particular chip target. At this point, OpenOCD continues running (as a server) in the foreground, waiting for interactive commands from socket connections.
    - See the flashing with SCons section for potential error messages.

1.  Without closing the running OpenOCD server, open a telnet connection to localhost:4444, the OpenOCD console.
    1.  In the OpenOCD console, run these commands:

        ```
        reset halt
        flash erase_sector 0 0 last
        flash write_image <path/to/your/image.bin>
        ```

        Note that the path to the image is relatively to where the OpenOCD server is running, not where your telnet client is running.

    1.  When you're ready to start your target, run this in the OpenOCD console:

        ```
        reset
        ```

    1.  When you're done, you can stop the OpenOCD server with:

        ```
        exit
        ```

    1.  This is only a small subset of commands available through OpenOCD. Those interested should read the [commands section of the official OpenOCD documentation](http://openocd.org/doc/html/General-Commands.html).

1.  Instead of running commands interactively, you can also automate the above by passing in commands to run through OpenOCD's arguments:

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

1.  OpenOCD integrates with command-line GDB, if that's your thing. Compared to a modern IDE, GDB has a steeper learning curve, so how to use GDB is outside the scope of this lab. However, [instructions for integrating OpenOCD and GDB are available](http://openocd.org/doc/html/GDB-and-OpenOCD.html).

### OpenOCD Target Configurations
Examples of the target config are:
- `target/lpc11xx.cfg` for LPC11C14 targets.
- `lpc1549_openocd.cfg` (in this repository) for LPC1549 targets.

## Repository Maintenance
Advanced topics for repository maintenance.
- [Updating mbed](UPDATING-MBED.md)