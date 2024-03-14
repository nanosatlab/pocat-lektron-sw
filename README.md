# <sup>po</sup>CAT Lektron

1. [Introduction](#introduction)
2. [Repository Organization](#organization)
3. [Installation](#installation)
    - [Requirements](#requirements)
    - [Steps](#steps)
        - [Git Basics](#git)
4. [Hacks](#hacks)
    - [printf in terminal](#printfinterminal)
    - [print float as strings](#printfloatasstring)
6. [Coding Conventions](#cocos)
    - [Formatting](#formatting)
    - [Comments](#comments)
    - [Syntactic conventions](#syntconv)
    - [Names](#names)
7. [Licence](#license)

## Introduction
<a name="introduction"></a>

This repository will contain that code that has been validated, or it is currently under development.

This will need to be extended. By now you shall find in this file the instructions to install and use Git, and GitHub Desktop. Please do not create branches until the code from the main branch has been uploaded. Also, The coding conventions are presented in this same document.

## Repository Organization
<a name="organization"></a>
In this repository several directories are found. They are organized as follows:

- **doc**: This folder contains useful documents such as the Coding conventions. The main documentation of the project is found in the wiki (available [here](https://wiki.nanosatlab.space/shelves/ieee-open-pocketqube-kit)).
- **src**: This folder shall contain the source code. It will be organized by subsystems. Other folders can be found such as: `CommonResources`, the `Core` where the files to start up the software are found, `Middleware` folder that contain 3rd parties FreeRTOS software, and `Semtech` where the libraries provided by the manufacturer are found.
- **misc**: In this folder configuration files will be found such as ./settings, 

## Installation
<a name="installation"></a>
In this section the installation of GitHub and the repository is provided. First the requirements are presented. Secondly the steps to install the requirements are presented.

### Requirements
<a name="requirements"></a>
The required software to collaborate developing this project is:

- **STM32CubeIDE**: This is the IDE used to develop the software development and debugging. It is available for different platforms (Linux and Windows in [here](https://www.st.com/en/development-tools/stm32cubeide.html)). The versions to use is 1.13.2.

- **gcc-9-2020-q2-update**: The GNU Arm Embedded Toolchain for C and C++ tools and assembly programs. (Available [here](https://developer.arm.com/downloads/-/gnu-rm/9-2020-q2-update)) 

- **Git**: In order to track the code developed for the required branch, it is mandatory to use Git. (Available [here](https://git-scm.com/downloads) It is the main resource for Linux users). It is important to remark that for those Windows users GitHub Desktop client is also available (Find it [here](https://desktop.github.com/)) Find the tutorials for both of them respectively here: [Git](https://git-scm.com/docs/gittutorial), [GitHub Desktop](https://docs.github.com/en/desktop/overview/getting-started-with-github-desktop). 

- **ST-Link V2 interface**: This is needed to program the PocketQubes or by developing the coed on the ST-Link V2. Please contact any of the contributors to get one.

### Steps
<a name="steps"></a>
To be done. Just follow the steps indicated for each software.

#### Git Basics:
<a name="git"></a>
You can create a new branch from the master at the GitHub website. 

![alt_text](doc/img/create_branch.png)

The following procedure is valid for both Windows, Mac and Linux users. If you are using Windows please open the git bash terminal. To clone the repository and being able to find it using git using and a SSH key (Please find the instructions to create and manage SSH keys in the following link: [here](https://docs.github.com/en/authentication/connecting-to-github-with-ssh)) we can use the following commands:

    git clone git@github.com:nanosatlab/pocat-lektron-sw.git

Once the directory is cloned, we can switch the branch by using:

    git switch <name-of-the-branch>

To look for all the available branches:

    git branch -a

Once we want to commit a change, we can use the following commands:

    git status                      # Check what changes had been done
    git add --all                   # Adding all the changes done
    git commit -m "your-message"    # Creating a commit and the corresponding message
    git push                        # Push all the changes to the remote trancing.

Congratulations, your code is now on [GitHub](https://github.com/nanosatlab/pocat-lektron-sw) :)

## Hacks
<a name="hacks"></a>

### Print data on terminal using printf
<a name="printfinterminal"></a>
In main add between section (USER CODE 4):
    
    /* USER CODE BEGIN 4 */
    int _write(int file, char *ptr, int len)
    {
    	int DataIdx;
    		for(DataIdx=0; DataIdx<len; DataIdx++)
    		{
    			ITM_SendChar(*ptr++);
            }
    		return len;
    }
    /* USER CODE END 4 */

Every time we want to print something in the terminal, we use: 
`printf("Hi, Lektron team");`

To activate: Winodws -> Show View -> SWV -> SWV IT Data Console -> Config

Activate port0, and finally, click the red button to start recording

### print float as strings
<a name="printfloatasstring"></a>

If we want to print a float, using printf, first we have to convert float to string and then print it using snprintf.

    snprintf(temperature_buff, sizeof(temperature_buff), "Temperature: %.2f", real_temperature); // real_temperature is a float

If we have the following error:

    The float formatting support is not enabled, check your MCU Settings from
		"Project Properties > C/C++ Build > Settings > Tool Settings",
		or add manually "-u _printf_float" in linker flags."

To solve that issue:

    Project > Propierties > C/C++ Build > Settings > Tool Settings > MCU GCC Linker > Miscellaneous > Other flags 

We add a new line: `-u _printf_float`


## Coding Conventions
<a name="cocos"></a>
In this section the coding conventions are specified. Please find them attached [here](https://www.gnu.org/prep/standards/html_node/Writing-C.html). The most important notes are summarized in the Coding Conventions file from the documentation (See it [here](doc/CodingConventions.md)).


## Licence
<a name="licencse"></a>

This file is part of a project developed at Nano-Satellite and Payload Laboratory (NanoSat Lab), Universitat Politècnica de Catalunya - UPC BarcelonaTech.

This repository is under the GNU GPL V3. Find a copy within this repository.
