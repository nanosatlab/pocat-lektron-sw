# <sup>po</sup>CAT Lektron
<img src="img/logo.png" alt="Image Description" width="200"/>


1. [Introduction](#introduction)
2. [Installation](#installation)
    - [Requirements](#requirements)
    - [Steps](#steps)
        - [Git Basics](#git)
3. [Coding Conventions](#cocos)
    - [Formatting](#formatting)
    - [Comments](#comments)
    - [Syntactic conventions](#syntconv)
    - [Names](#names)
4. [Licence](#license)

## Introduction
<a name="introduction"></a>

This repository will contain that code that has been validated, or it is currently under development.

This will need to be extended. By now you shall find in this file the instructions to install and use Git, and GitHub Desktop. Please do not create branches until the code from the main branch has been uploaded. Also, The coding conventions are presented in this same document.

## Installation
<a name="installation"></a>
In this section the installation of GitHub and the repository is provided. First the requirements are presented. Secondly the steps to install the requirements are presented.

### Requirements
<a name="requirements"></a>
The required software to collaborate developing this project is:
- **STM32CubeIDE**: This is the IDE used to develop the software development and debugging. It is available for different platforms (Linux and Windows in [here](https://www.st.com/en/development-tools/stm32cubeide.html))
- **gcc-9-2020-q2-update**: The GNU Arm Embedded Toolchain for C and C++ tools and assmebly programs. (Available [here](https://developer.arm.com/downloads/-/gnu-rm/9-2020-q2-update)) 
- **Git**: In order to track the code developed for the required branch, it is mandatory to use Git. (Available [here](https://git-scm.com/downloads) It is the main resource for Linux users). It is important to remark that for those Windows users GitHub Desktop client is also available (Find it [here](https://desktop.github.com/)) Find the tutorials for both of them respectively here: [Git](https://git-scm.com/docs/gittutorial), [GitHub Desktop](https://docs.github.com/en/desktop/overview/getting-started-with-github-desktop). 

### Steps
<a name="steps"></a>
To be done. Just follow the steps indicated for each software.

#### Git Basics:
<a name="git"></a>
You can create a new branch from the master at the GitHub website. 

![alt_text](img/create_branch.png)

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

## Coding Conventions
<a name="cocos"></a>
In this section the coding conventions are specified. Please find them attached [here](https://www.gnu.org/prep/standards/html_node/Writing-C.html). The most important notes are summarized below.

### Formatting
<a name="formatting"></a>

- **Line Length**: Keep each line of your code to 79 characters or fewer. This ensures readability across different environments.
- **Function Formatting**: Begin the body of a C function with an open-brace ({) in the first column. This helps tools identify the start of functions. However, avoid placing open-braces in column one if they're inside a function to prevent misinterpretation.
```c
static char *
concat (char *s1, char *s2)
{
    // Function body
}
```
- **Function Names**: Start the name of a function in the first column. (See example below). This aids in searching for function definitions and assists certain tools in recognizing them.
- **Argument Splitting**: If function arguments don't fit on one line, split them onto multiple lines, aligning subsequent lines with the first argument.
```c
int
lots_of_args (int an_integer, long a_long, short a_short,
              double a_double, float a_float)
{
    // Function body
} 
```
- **Structs and Enums**: Place braces ({}) for struct and enum types in column one, unless the contents fit on one line.
```c
struct foo
{
    int a, b;
}
```
or
```c
struct foo { int a, b; }
```
- **Coding Style**: The style used is the default C coder provided by the STM32CubeIDE.
- **Expression Formatting**: Include spaces before open-parentheses and after commas. Split expressions into multiple lines before operators. Use extra parentheses for clarity and consistent indentation, especially when dealing with complex expressions.
```c
if (foo_this_is_long && bar > win (x, y, z)
    && remaining_condition)
{
    // Code block
}
```
- **Loop Formatting**: Format do-while statements with the do statement on one line, followed by braces around the loop body.
```c
do
{
    // Loop body
}
while (a > 0);
```
Remember, maintaining consistent formatting throughout your codebase enhances readability and makes collaboration easier.

### Comments
<a name="comments"></a>

- **Program Comments**: Every file should start with a comment briefly explaining its purpose. Please use the provided header as it already contains the maximum length specified previously.
```c
/*************************************************************************//**
 *  Name of the file & Breif description.
 *  @author     Name (Initials), e-mail
 *  @date       YYYY-month-DD
 *  @copyright  This file is part of a project developed at Nano-Satellite and
 *              Payload Laboratory (NanoSat Lab), Universitat Politècnica de
 *              Catalunya - UPC BarcelonaTech.
 ****************************************************************************/
```
- **Function Comments**: Comment on each function, explaining what it does, the types of arguments it receives, and the possible values of arguments. Additionally, clarify the significance of the return value, if applicable.
```c
/*************************************************************************//**
     * Comment on the function. explain what it does, and how it does it if
     * necessary.
     *
     * @param     Parameter passed brefly explained
     * @return    Return of the function
     * @see       If there is need to specify a reference (function
     *            source library or API)
 ****************************************************************************/
```
- **Writing Style**: Follow a consistent writing style for comments. Use English for GNU programs for universal readability among programmers. Write complete sentences, capitalize the first word, and use two spaces after the end of each sentence for compatibility with Emacs sentence commands.
- **Variable Comments**: Include comments for each static variable to clarify its purpose and usage.
```c
/* Nonzero means truncate lines in the display;
   zero means continue them.  */
int truncate_lines;
```

### Syntactic Conventions
<a name="syntconv"></a>

- **Explicit Type Declarations**: Declare the types of all objects explicitly. For example, explicitly declare all arguments to functions and specify the return type of functions rather than omitting `int`.
- **Variable Naming and Scope**: Use meaningful names for local variables and declare a separate variable for each distinct purpose. Avoid reusing the same local variables repeatedly. Also, declare variables in the smallest scope possible.
- **Multiple Variable Declarations**: Avoid declaring multiple variables in one declaration that spans lines. Start a new declaration on each line for clarity.
```c
int foo, bar;
```
or
```c
int foo;
int bar;
```
If they are global variables they must have a comment preceding it.
- **Bracing Conventions**: Always use braces around the `if-else` statement nested in another `if` statement for clarity and consistency.
```c
if (foo)
  {
    if (bar)
      win ();
    else
      lose ();
  }
```
- **Structure Tag Declaration**: Don’t declare both a structure tag and variables or typedefs in the same declaration. Declare the structure tag separately and then use it to declare the variables or typedefs.
```c
struct Point {
    int x;
    int y;
};

struct Point p1;
```
- **Assignments in Conditions**: Try to avoid assignments inside `if` conditions; instead, separate the assignment and the condition for better readability. Don't write this:
```c
if ((foo = (char *) malloc (sizeof *foo)) == NULL)
  fatal ("virtual memory exhausted");
```
Write this:
```c
foo = (char *) malloc (sizeof *foo);
if (foo == NULL)
  fatal ("virtual memory exhausted");
```
### Names
<a name="names"></a>

- **Global Variables and Functions**: Choose names that provide meaningful information about the variable or function's purpose. Use English names in GNU programs for universal readability.
```c
int ignore_space_change_flag; // Indicates whether to ignore changes in horizontal whitespace (-b).
```
- **Local Variables**: Local variable names can be shorter since they are used within a specific context where comments explain their purpose.
- **Abbreviations**: Limit the use of abbreviations in symbol names. If abbreviations are used, explain their meaning and use them consistently.
- **Underscores and Case**: Use underscores to separate words in a name for readability, and stick to lower case. Reserve upper case for macros, enum constants, and name-prefixes following a uniform convention.
```c
int ignore_space_change_flag; // Good
int iCantReadThis; // Avoid
```
- **Command-Line Options**: Name variables indicating command-line options after the meaning of the option, not the option-letter. Include a comment stating both the exact meaning of the option and its letter.

- **Enum vs. #define**: When defining names with constant integer values, use enum instead of #define for better compatibility with tools like GDB.
```c
// Using #define
#define MONDAY 0
#define TUESDAY 1
#define WEDNESDAY 2
#define THURSDAY 3
#define FRIDAY 4

// Using enum
enum Weekday {
    MON = 0,
    TUE,
    WED,
    THU,
    FRI
};
```

## Licence
<a name="licencse"></a>

This file is part of a project developed at Nano-Satellite and Payload Laboratory (NanoSat Lab), Universitat Politècnica de Catalunya - UPC BarcelonaTech.

This repository is under the GNU GPL V3. Find a copy within this repository.