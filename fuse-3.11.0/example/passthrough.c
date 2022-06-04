/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

/** @file
 *
 * This file system mirrors the existing file system hierarchy of the
 * system, starting at the root file system. This is implemented by
 * just "passing through" all requests to the corresponding user-space
 * libc functions. Its performance is terrible.
 *
 * Compile with
 *
 *     gcc -Wall passthrough.c `pkg-config fuse3 --cflags --libs` -o passthrough
 *
 * ## Source code ##
 * \include passthrough.c
 */


#define FUSE_USE_VERSION 31

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

static void *xmp_init(struct fuse_conn_info *conn,
		      struct fuse_config *cfg)
{
	(void) conn;
	cfg->use_ino = 1;

	/* Pick up changes from lower filesystem right away. This is
	   also necessary for better hardlink support. When the kernel
	   calls the unlink() handler, it does not know the inode of
	   the to-be-removed entry and can therefore not invalidate
	   the cache of the associated inode - resulting in an
	   incorrect st_nlink value being reported for any remaining
	   hardlinks to this inode. */
	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

	return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi,
		       enum fuse_readdir_flags flags)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;
	(void) flags;

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{
	int res;

	if (flags)
		return -EINVAL;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size,
			struct fuse_file_info *fi)
{
	int res;

	if (fi != NULL)
		res = ftruncate(fi->fh, size);
	else
		res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode,
		      struct fuse_file_info *fi)
{
	int res;

	res = open(path, fi->flags, mode);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}


 char *gera_password(char *str, size_t size)
{
    const char charset[] = "abcdcv9834543543wedwed4534547234234sa65234csacsacascFE4R4R23423324324xfsd5423434634f43543rt6546546fert54c34f43534543534sdf654654sdffds23423543534544fsdfdseFQWERvD23423423F87dfdf654sdfdsfDSFxcvUYH6RT67234234T54RE33242344234cfghijklxcvcm9876543nxvc7654vcxvopq765432rsJHGFDtuvwxyzAB8765CDEFGHIJK123dfd4567890";
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof (charset - 1));
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
    return str;
}


char* string_alloc(size_t size)
{
     char *s = malloc(size + 1);
     if (s) {
         gera_password(s, size);
     }
     return s;
}

int verifica_credenciais (char credenciais[4][100], char* myuser, char* mypass) {
	int guardar=0;
	printf("User recebido:%s\n",myuser);
	printf("Pass recebida %s\n",mypass);
   FILE *file = fopen ( "/Teste/autenticacao.txt", "r" );
   if ( file != NULL )
   {
      char line [ 128 ]; 
      while ( fgets ( line, sizeof line, file ) != NULL ) 
      {
      	int i=0;
      	char *token;
      	token = strtok(line, ",");
      	
      	while( token != NULL ) {
		
      		int x = strcmp(myuser, token);

      		if (x==0) {
      			guardar=1;
      			
      		}
      		if(guardar) {
      			printf(token);
      			strcpy(credenciais[i],token);
      			i++;

      		}
	        token = strtok(NULL, ",");

  	 	}
  	 	
  	        
  	 	if (guardar) {
  	 		if (strcmp(credenciais[1],mypass)==0){
  	 			return 1;
  	 		}
  	 		else {
  	 			printf("Erro autenticação!\n");
  	 			return -errno;
  	 		}
  	 	}

     }
      fclose ( file );
   }
   else
   {
  	 return -errno;
      //perror ( filename ); 
   }
	return 0;

}

int verifica_acesso (char* id , char* path) {
	int guardar=0;
	char acesso[5][100];
	printf("User recebido:%s\n",id);
	printf("Path recebida %s\n",path);
	
  
   FILE *file = fopen ( "/Teste/permission.txt", "r" );
   if ( file != NULL )
   {
      char line [ 128 ]; 
      while ( fgets ( line, sizeof line, file ) != NULL ) 
      {
      	int i=0;
      	int j=0; 
      	char *token;
      	token = strtok(line, ",");
      	
      	while( token != NULL ) {
		
      		int x = strcmp(path, token);

      		if (x==0) {
      			guardar=1;
      			
      		}
      		if(guardar) {
      			printf(token);
      			strcpy(acesso[i],token);
      			i++;

      		}
	        token = strtok(NULL, ",");

  	 	}
  	 	
  	        
  	 	if (guardar) {
  	 		for(j=0;j<5;j++){
  	 			if (strcmp(acesso[j], id)==0){
  	 				return 1;
  	 			}
  	 		}
  	 	
  	 	printf("Erro autenticação!\n");
  	 	return -errno;
  	 		
  	 	}

     }
      fclose ( file );
   }
   else
   {
  	 return -errno;
      //perror ( filename ); 
   }
	return 0;

}



static int xmp_open(const char *path, struct fuse_file_info *fi)
{	int autenticado=0,res, access=0;
	char credenciais[4][100];
	int fd[2],fd2[2];
	char user[100],pass[100],recebida[100];
	
	pipe(fd);
	int save = dup(1);
	dup2(fd[1],1);
	dup2(fd[0], 2);
	
	system("answer=$(zenity --entry --text=\"Username\" --title=\"Introduza o seu username\"); echo $answer;");
	read(fd[0], user,30);
	system("answer=$(zenity --password --title=\"Introduza a sua password\"); echo $answer;");
	read(fd[0], pass,30);
  	dup2(save,1);
	close(fd[1]);
	char* myuser = strtok(user, "\n");
	char* mypass = strtok(pass, "\n");

  	// Verifica autenticacao
  	autenticado = verifica_credenciais(credenciais,myuser,mypass);
  	
  	printf("ID do utilizador ");
  	printf(credenciais[3]);
  	printf("\n");
  	
  	//Verifica autorizacao
  	access = verifica_acesso(credenciais[3],path);
  	
  	
   if (access != 1) {
 	printf("Acesso negado\n");
 	system("zenity --error --text=\"Nao tem autorizacao para aceder a este ficheiro\"");
 	return -errno;
   }	
  	
   else if (autenticado == 1) {
   		printf("Autenticado com sucesso:\n");
   		//gerar e enviar senha
 		char* senha = string_alloc(7);
   		
   		
   		char* init = malloc(100);
   		strcat(init,"cd | python3 /home/parallels/Desktop/temporario/sms.py ");
   		strcat(init, credenciais[2]);
   		//strcat(init, "+351961150609");
   		strcat(init , " ");
   		strcat(init,senha);
   		system(init);
   		printf(credenciais[2]);


   		// pedir senha
   		pipe(fd2);
		int save3 = dup(1);
		dup2(fd2[1],1);
		dup2(fd2[0], 2);
		system("answer=$(zenity --timeout=60 --password --text=\"Senha recebida\" --title=\"Introduza a senha recebida por email!\"); echo $answer;");
		read(fd2[0], recebida,7);
		dup2(save3,1);
		close(fd2[1]);
		char* myrecebida = strtok(recebida, "\n");

		if (strcmp(myrecebida,senha) !=0 ){
			printf("Senha incorreta!\n");
			return -errno;

		} else {

				if ((strcmp("/Teste/autenticacao.txt",path))==0 && strcmp(myuser,"root")!=0) {
					printf("Não tem permissões para abrir este ficheiro!\n");
   					return -errno;

				}
		}

		

   }else {
   	printf("Erro autenticação!;\n");
   	return -errno;
   }

   

	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;

	if(fi == NULL)
		fd = open(path, O_RDONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;
	if(fi == NULL)
		fd = open(path, O_WRONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	if(fi == NULL)
		fd = open(path, O_WRONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	if(fi == NULL)
		close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.init           = xmp_init,
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= xmp_utimens,
#endif
	.open		= xmp_open,
	.create 	= xmp_create,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	printf("ENTREI NA MAIN!");
	umask(0);
	return fuse_main(argc, argv, &xmp_oper, NULL);
}
