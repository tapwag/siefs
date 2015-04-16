#ifndef VMCONVERT_H
#define VMCONVERT_H

int write_riff_header(void *buffer, int vmo_file_size);
int vmo_start();
void vmo_decode(void *vmo_frame, void *buffer);
void vmo_end();

#endif

