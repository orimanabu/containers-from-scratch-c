#define _GNU_SOURCE

#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

#define STACK_SIZE 1024 * 1024
#define ROOTFS "/home/ori/ubuntu-rootfs"
#define HOSTNAME "mycontainer"

struct cmd_arg {
	int argc;
	char **argv;
};

#if 0
static int chroot_with_chroot(char *new_root)
{

	printf("** chroot\n");
	int ret_chroot = chroot(new_root);
	if (ret_chroot == -1) {
		printf("chroot failed: %s\n", strerror(errno));
		return -1;
	}

	printf("** chdir /\n");
	int ret_chdir = chdir("/");
	if (ret_chdir == -1) {
		printf("chdir failed: %s\n", strerror(errno));
		return -1;
	}

	printf("** mount /proc\n");
	int ret_mount = mount("proc", "/proc", "proc", 0, NULL);
	if (ret_mount == -1) {
		printf("mount failed: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}
#else
static long int pivot_root(const char *new_root, const char *put_old)
{
	return syscall(SYS_pivot_root, new_root, put_old);
}

static int chroot_with_pivot_root(char *new_root)
{
	char path[PATH_MAX];
	const char *put_old = "/old_root";
	struct stat st;

	/* Ensure that 'new_root' and its parent mount don't have
	   shared propagation (which would cause pivot_root() to
	   return an error), and prevent propagation of mount
	   events to the initial mount namespace. */
	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
		err(EXIT_FAILURE, "mount-MS_PRIVATE");

	/* Ensure that 'new_root' is a mount point. */
	printf("** bind mount new_root\n");
	if (mount(new_root, new_root, NULL, MS_BIND, NULL) == -1)
		err(EXIT_FAILURE, "mount-MS_BIND");

	/* Create directory to which old root will be pivoted. */
	snprintf(path, sizeof(path), "%s/%s", new_root, put_old);
	if (stat(path, &st) == -1) {
		if (errno == ENOENT) {
			printf("XXX %s does not exist, creating...\n", path);
			if (mkdir(path, 0777) == -1) {
				err(EXIT_FAILURE, "mkdir");
			}
		} else {
			err(EXIT_FAILURE, "stat");
		}
	} else {
		printf("XXX %s exists\n", path);
		if (S_ISDIR(st.st_mode)) {
			printf("XXX %s is a directory\n", path);
		} else {
			err(EXIT_FAILURE, "XXX %s is not a directory\n", path);
		}
	}

	/* And pivot the root filesystem. */
	printf("** pivot_root/\n");
	if (pivot_root(new_root, path) == -1)
		err(EXIT_FAILURE, "pivot_root");

	/* Switch the current working directory to "/". */
	printf("** chddir /\n");
	if (chdir("/") == -1)
		err(EXIT_FAILURE, "chdir");

	printf("** mount /proc\n");
	int ret_mount = mount("proc", "/proc", "proc", 0, NULL);
	if (ret_mount == -1) {
		printf("mount failed: %s\n", strerror(errno));
		return -1;
	}

#if 0
	/* Unmount old root and remove mount point. */
	printf("** unmount old_root\n");
	if (umount2(put_old, MNT_DETACH) == -1)
		perror("umount2");

	printf("** rmdir old_root\n");
	if (rmdir(put_old) == -1)
		perror("rmdir");
#endif
	return 0;
}
#endif

int run_in_misc_ns(void *arg)
{
	struct cmd_arg *cmdarg = (struct cmd_arg *)arg;

	printf("* Running in UTS/PID/Mount namespace: PID = %d, PPID = %d\n", getpid(), getppid());

	int ret;
#if 0
	ret = chroot_with_chroot(ROOTFS);
#else
	ret = chroot_with_pivot_root(ROOTFS);
#endif
	if (ret == -1) {
		return ret;
	}

	/* We can change hostname since we are already in UTS namespace. */
	if (sethostname(HOSTNAME, strlen(HOSTNAME)) == -1) {
		perror("sethostname");
		return -1;
	}

	execvp(cmdarg->argv[1], &(cmdarg->argv[1]));
	perror("execv failed");
	return -1;
}

int run_in_user_ns(void *arg)
{
	struct cmd_arg *cmdarg = (struct cmd_arg *)arg;
	const char *uid_map = "0 1000 1\n";
	const char *gid_map = "0 1000 1\n";
	FILE *file;

	printf("* Running in User namespace: PID = %d, PPID = %d\n", getpid(), getppid());

	printf("** create /proc/self/uid_map\n");
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

	printf("** write 'deny' to /proc/self/setgroups\n");
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

	printf("** create /proc/self/gid_map\n");
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

	pid_t pid2 = clone(run_in_misc_ns, stack2 + STACK_SIZE, CLONE_NEWUTS|CLONE_NEWPID|CLONE_NEWNS|SIGCHLD, cmdarg);
	if (pid2 == -1) {
		perror("clone child2");
		exit(EXIT_FAILURE);
	}

	waitpid(pid2, NULL, 0);
	free(stack2);

	printf("* Exited from UTS/PID/Mount namespace.\n");
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

	printf("* Exited from User namespace.\n");
	return 0;
}
