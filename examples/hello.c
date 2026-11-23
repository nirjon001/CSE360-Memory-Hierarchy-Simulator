/* Tiny standalone program: use it to check that VS Code + gcc work.
 * In VS Code: Ctrl+Shift+P -> "Tasks: Run Task" -> "Run active C file". */
#include <stdio.h>

int main(void)
{
    printf("C toolchain works! Compiled with gcc.\n");
    return 0;
}
