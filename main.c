#define _GNU_SOURCE

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <sys/mount.h>
#include <fcntl.h>

#define STACK_SIZE 1024 * 1024
#define ROOTFS "/home/ori/ubuntu-rootfs"

struct cmd_arg {
	int argc;
	char **argv;
};

int run_in_misc_ns()
{
	printf("Child2 (run_in_misc_ns): PID = %d, PPID = %d\n", getpid(), getppid());

	printf("* chroot\n");
	int ret_chroot = chroot(ROOTFS);
	if (ret_chroot == -1) {
		printf("chroot failed: %s\n", strerror(errno));
		return 1;
	}

	printf("* chdir /\n");
	int ret_chdir = chdir("/");
	if (ret_chdir == -1) {
		printf("chdir failed: %s\n", strerror(errno));
		return 1;
	}

	printf("* mount /proc\n");
	int ret_mount = mount("proc", "/proc", "proc", 0, NULL);
	if (ret_mount == -1) {
		printf("mount failed: %s\n", strerror(errno));
		return 1;
	}

	execlp("bash", "bash", NULL);
	perror("execlp failed");
	return -1;

	return 0;
}

int run_in_user_ns(void *arg)
{
	struct cmd_arg *cmdarg = (struct cmd_arg *)arg;

	printf("Child1 (run_in_user_ns): PID = %d, PPID = %d\n", getpid(), getppid());
	printf("  argc: %d\n", cmdarg->argc);

	const char *uid_map = "0 1000 1\n";
	const char *gid_map = "0 1000 1\n";
	FILE *file;

	printf("* create /proc/self/uid_map\n");
	if ((file = fopen("/proc/self/uid_map", "w")) == NULL) {
		perror("Failed to open /proc/self/uid_map");
		return -1;
	}
	if (fwrite(uid_map, 1, strlen(uid_map), file) != strlen(uid_map)) {
		perror("Failed to write to /proc/self/uid_map");
		fclose(file);
		return -1;
	}
	fclose(file);

	printf("* write 'deny' to /proc/self/setgroups\n");
	if ((file = fopen("/proc/self/setgroups", "w")) == NULL) {
		perror("Failed to open /proc/self/setgroups");
		return -1;
	}
	if (fwrite("deny", 1, strlen("deny"), file) != strlen("deny")) {
		perror("Failed to write to /proc/self/setgroups");
		fclose(file);
		return -1;
	}
	fclose(file);

	printf("* create /proc/self/gid_map\n");
	if ((file = fopen("/proc/self/gid_map", "w")) == NULL) {
		perror("Failed to open /proc/self/gid_map");
		return -1;
	}
	if (fwrite(gid_map, 1, strlen(gid_map), file) != strlen(gid_map)) {
		perror("Failed to write to /proc/self/gid_map");
		fclose(file);
		return -1;
	}
	fclose(file);

	char *stack2 = malloc(STACK_SIZE);
	if (!stack2) {
		perror("malloc for stack2");
		exit(EXIT_FAILURE);
	}

	pid_t pid2 = clone(run_in_misc_ns, stack2 + STACK_SIZE, CLONE_NEWUTS|CLONE_NEWPID|CLONE_NEWNS|SIGCHLD, NULL);
	if (pid2 == -1) {
		perror("clone child2");
		exit(EXIT_FAILURE);
	}

	waitpid(pid2, NULL, 0);
	free(stack2);

	printf("Child1: Exiting after Child2.\n");
	return 0;
}

int main(int argc, char *argv[])
{
	struct cmd_arg cmdarg = {argc, argv};

	char *stack1 = malloc(STACK_SIZE);
	if (!stack1) {
		perror("malloc for stack1");
		exit(EXIT_FAILURE);
	}

	pid_t pid1 = clone(run_in_user_ns, stack1 + STACK_SIZE, CLONE_NEWUSER|SIGCHLD, (void *)&cmdarg);
	if (pid1 == -1) {
		perror("clone child1");
		exit(EXIT_FAILURE);
	}

	waitpid(pid1, NULL, 0);
	free(stack1);

	printf("Parent: children have exited.\n");
	return 0;
}
