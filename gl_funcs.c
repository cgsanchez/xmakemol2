/*

Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 Matthew P. Hodges
This file is part of XMakemol.

XMakemol is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

XMakemol is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XMakemol; see the file COPYING.  If not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/


#define __GL_FUNCS_C__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef GL

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <Xm/Xm.h>

#ifdef SGI_STEREO
#include <X11/extensions/SGIStereo.h> /* aro */
#endif /* SGI_STEREO */

#include "globals.h"
#include "bbox.h"
#include "bonds.h"
#include "defs.h"
#include "draw.h"
#include "view.h"

#include "gl_funcs.h"
#include "gl2ps.h"

/* Function prototypes */

double get_atom_scale (void);
double get_bond_scale (void);
double get_bond_width (void);
double get_hbond_scale (void);
double gl_get_rendered_atom_size (int);
void gl_init (void);
void gl_render_atoms (void);
void gl_render_bbox (void);
void gl_render_bbox_face (int, int, int, int);
void gl_render_bbox_line (int, int);
void gl_render_bonds (void);
void gl_render_hbonds (void);
void gl_render_vectors (void);
void gl_set_perspective (void);
double get_smallest_cov_rad (void);
void gl_update_near_and_far (void);
double mod_of_vec (double *);
Boolean mouse_motion_p (void);
void normalize_vec (double *);
Boolean outline_mode_p (void);

/* Variable declarations */

static int gl_copy_canvas = 0;
static int gl_initialized = 0;
static int gl_no_atom_segments = 12;
static int gl_no_bond_segments = 8;
static int gl_use_lighting = 0;
static int gl_use_specular_lighting = 0;
static int gl_sep = 30; /* aro */
static GLfloat gl_shininess[] = {64};

static double gl_eye = 10;         /* per-system setting */
static double gl_fov = 50;
static double gl_max_z, gl_min_z;
static double gl_perspective_near = 0, gl_perspective_far = 0;

static GLUquadricObj *gl_quadric = NULL;

static enum gl_render_types gl_render_type = BALL_AND_STICK;
static enum gl_render_types gl_current_render_type;

void gl_render (void)
{
  enum render_stereo_types get_gl_render_stereo();
  void set_gl_render_stereo(enum render_stereo_types);
  void StereoProjection(float left, float right, float bottom,
                        float top, float near, float far,
                        float zero_plane, float dist, float eye,
                        int side_by_side);
  double get_canvas_scale(void);
  double get_z_depth(void);
  
  double eye_offset, gl_fov_rad, zp_width; /* zero parallax width */
  static double last_eye_offset;
  
  double scale, depth, width_scale, height_scale;

  int side_by_side;

  if (gl_initialized == 0)
    {
      gl_init ();
      gl_initialized = 1;
    }

  gl_set_perspective ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  /* Make sure the image does not appear distorted in a window that is wide
     but not very tall.  Note that we use canvas_scale and z_depth here for
     trying to keep up with the X11 rendering engine.  Currently the
     selection from the OpenGL canvas is performed using the coordinates
     generated by the X11 rendering engine.  In the following code we are
     trying to squeeze the OpenGL view so that the image generated by the
     OpenGL resembles the image generated by the X11 rendered as much as
     possible.  This depends highly on the position of the field of view
     slider but we are trying to match the images anyway.  The images become
     more and more different as the user zooms in. */
  scale = get_canvas_scale ();
  depth = get_z_depth ();   
  width_scale = (scale * depth) / (float) canvas_width;
  height_scale = width_scale * (canvas_width / (float) canvas_height);    
  glScalef (width_scale, height_scale, 1.0f);

  gluLookAt (0.0, 0.0, gl_eye, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);

  /* This may get changed in StereoProjection, but isn't reset when we
     change back to a single image */
  glViewport (0, 0, canvas_width, canvas_height);

  /* aro--> */
  if(get_gl_render_stereo() && depth_is_on)
    {
      /* Calculate offset between eyes.  Max distance between eyes should be 
         (distance to projection plane) / 15 */
      eye_offset = (gl_eye + gl_perspective_near) / gl_sep;
      if(eye_offset > 0.0)
        last_eye_offset = eye_offset;
      else
        eye_offset = last_eye_offset;
      
      /* Calculate width at zero parallax (default z = 0) from given fov. 
         zp_width = (1/2) entire width at zero parallax */
      gl_fov_rad = (PI * gl_fov) / 180;
      zp_width = gl_eye * tan( 0.5 * gl_fov_rad );
      
      /* Left eye: */

      /* Create nonsymetric viewing frustum */
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();

      if (get_gl_render_stereo() == SIDE_BY_SIDE)
        {
          side_by_side = 1;
        }
      else
        {
          side_by_side = 0;
        }

      StereoProjection(-zp_width, zp_width, -zp_width, zp_width, 
                       gl_perspective_near, gl_perspective_far, 
                       0.0, gl_eye, -eye_offset, side_by_side);

      if(get_gl_render_stereo() == SIDE_BY_SIDE) { 
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
/*         glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); */
      }
      else if (get_gl_render_stereo() == RED_BLUE)
        {
          glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
          glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);/* Red mask */
        }
#ifdef SGI_STEREO
      else if(get_gl_render_stereo() == SGI_HARDWARE) {
        glXWaitGL();
        XSGISetStereoBuffer(display, XtWindow(canvas), STEREO_BUFFER_LEFT);
        glXWaitX();
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Need to change aspect ratio as we've changed aspect ratio of monitor */
        width_scale = width_scale / 0.48;
      }
#endif /* SGI_STEREO */
      else
        fprintf(stderr, "gl_render: Invalid stereo mode\n");

      glMatrixMode (GL_MODELVIEW);
      glLoadIdentity ();
      glScalef (width_scale, height_scale, 1.0f);
    
      if ((! mouse_motion_p ()) || (! outline_mode_p ()))
        {
          if (gl_use_lighting)
            {
              gluQuadricNormals (gl_quadric, GLU_SMOOTH);
            }
          else
            {
              gluQuadricNormals (gl_quadric, GLU_NONE);
            }
          gluQuadricDrawStyle (gl_quadric, GLU_FILL);
          
          glEnable (GL_DEPTH_TEST);
          glEnable (GL_COLOR_MATERIAL);

          glDisable (GL_LIGHTING);
          if (bbox_flag && (no_atoms > 0)) gl_render_bbox ();
          if (gl_use_lighting) glEnable (GL_LIGHTING);
          
          gl_render_atoms ();
          gl_render_bonds ();
          gl_render_hbonds ();
          gl_render_vectors ();
        }
      else
        {
          glDisable (GL_LIGHTING);
          glDisable (GL_DEPTH_TEST);
          glDisable (GL_COLOR_MATERIAL);

          gluQuadricDrawStyle (gl_quadric, GLU_LINE);
          gluQuadricNormals (gl_quadric, GLU_NONE);

          gl_render_atoms ();
        }

      /* Right eye: */

      /* Create nonsymetric viewing frustum */
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();

      StereoProjection(-zp_width, zp_width, -zp_width, zp_width, 
                       gl_perspective_near, gl_perspective_far, 
                       0.0, gl_eye, eye_offset, -side_by_side);

      if(get_gl_render_stereo() == SIDE_BY_SIDE) {
/*         glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); */
/*         glClear(GL_DEPTH_BUFFER_BIT); */
      }
      else if (get_gl_render_stereo() == RED_BLUE)
        {
          glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_TRUE);/* Blue mask */
          glClear(GL_DEPTH_BUFFER_BIT);
        }
#ifdef SGI_STEREO
      else if(get_gl_render_stereo() == SGI_HARDWARE) {
        glXWaitGL();
        XSGISetStereoBuffer(display, XtWindow(canvas), STEREO_BUFFER_RIGHT);
        glXWaitX();
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
      }
#endif /* SGI_STEREO */
      else
        fprintf(stderr, "gl_render: Invalid stereo mode\n");
                
      glMatrixMode (GL_MODELVIEW);
      glLoadIdentity ();
      glScalef (width_scale, height_scale, 1.0f);
    }
  else /* If not stereo, we need to clear color and depth */
    {
      glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    }
  /* <--aro */

  if ((! mouse_motion_p ()) || (! outline_mode_p ()))
    {
      if (gl_use_lighting)
        {
          gluQuadricNormals (gl_quadric, GLU_SMOOTH);
        }
      else
        {
          gluQuadricNormals (gl_quadric, GLU_NONE);
        }
      gluQuadricDrawStyle (gl_quadric, GLU_FILL);

      glEnable (GL_DEPTH_TEST);
      glEnable (GL_COLOR_MATERIAL);

      glDisable (GL_LIGHTING);
      if (bbox_flag && (no_atoms > 0)) gl_render_bbox ();
      if (gl_use_lighting) glEnable (GL_LIGHTING);

      gl_render_atoms ();
      gl_render_bonds ();
      gl_render_hbonds ();
      gl_render_vectors ();
    }
  else
    {
      glDisable (GL_LIGHTING);
      glDisable (GL_DEPTH_TEST);
      glDisable (GL_COLOR_MATERIAL);

      gluQuadricDrawStyle (gl_quadric, GLU_LINE);
      gluQuadricNormals (gl_quadric, GLU_NONE);
      
      gl_render_atoms ();
    }

  /* aro - change color mask so all of RGB color fields are cleared */
  if ((get_gl_render_stereo() == RED_BLUE) && depth_is_on)
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  
  /* Note that we are drawing onto the canvas directly, since I
     haven't been able to figure out the cause of crashes (in
     glClearColor) on some displays. */

  glXSwapBuffers (display, XtWindow (canvas));

  /* Note that we should make this conditional; we only need it when
     XpmWriteFileFromPixmap is called and canvas_pm is used. */

  if (gl_copy_canvas)
    {
      XCopyArea (display, XtWindow (canvas), canvas_pm, gc, 0, 0,
                 canvas_width, canvas_height, 0, 0);
    }
}

void StereoProjection(float left, float right, float bottom, float top,
                      float near, float far, float zero_plane, float dist,
                      float eye, int side_by_side)
{
  enum render_stereo_types get_gl_render_stereo();
  
  float xmid,
    ymid,
    clip_near,
    clip_far,
    topw,
    bottomw,
    leftw,
    rightw,
    dx,
    dy,
    n_over_d;
  
  static double last_near;
  static double last_far;

  /* Viewports for side by side mode */

  if (side_by_side == 1)
    {
      glViewport (canvas_width / 2, 0, canvas_width / 2, canvas_height);
      left *= 0.5;
      right *= 0.5;
    }
  else if (side_by_side == -1)
    {
      glViewport (0, 0, canvas_width, canvas_height);
      glColor3f (0.0, 0.0, 0.0);
      glLineWidth (2.0);

      glBegin (GL_LINES);
      /* FIXME: do this properly */
      glVertex3f (0.0, -1000, 0.0);
      glVertex3f (0.0, 1000, 0.0);
      glEnd ();

      glViewport (0, 0, canvas_width / 2, canvas_height);
      left *= 0.5;
      right *= 0.5;
    }
  else
    {
      glViewport (0, 0, canvas_width, canvas_height);
    }

  dx = right - left;
  dy = top - bottom;
  
  xmid = (right + left) / 2.0;
  ymid = (top + bottom) / 2.0;
  
  /* If the near clipping plane is going to be behind the eye, clamp its 
     position at that of the eye (We set it 1.0 first because glFrustum
     requires it to be non-zero */
  if(near > 0.0)
    { 
      last_near = clip_near = near;
      last_far = clip_far = far;
    }
  else
    {
      clip_near = last_near;
      clip_far = last_far;
    }
  
  n_over_d = clip_near / dist;
  
  topw = n_over_d * dy / 2.0;
  bottomw = -topw;
  rightw = n_over_d * (dx / 2.0 - eye);
  leftw  = n_over_d *(-dx / 2.0 - eye);

  /* Need to be in projection mode for this. */
  glLoadIdentity();
  glFrustum(leftw, rightw, bottomw, topw, clip_near, clip_far);
  glTranslatef(-xmid - eye,  -ymid,  -zero_plane - dist);
}

void gl_render_atoms (void)
{
  void gl_render_atom (int);
  int get_gl_render_type (void);

  int i;

  /* Return if atoms not being display and in ball and stick mode;
     note that we draw atoms if tubes mode if the bond is visible */

  for (i = 0; i < no_atoms; i++)
    {
      gl_current_render_type = (atoms[i].gl_render_type == DEFAULT) ?
        get_gl_render_type () : atoms[i].gl_render_type;
        
      if ((atom_flag == 0) &&
          ((gl_current_render_type == BALL_AND_STICK) ||
           ((gl_current_render_type == TUBES) && (! gl_use_lighting)))) return;

      if ((atoms[i].visi == 1) || (view_ghost_atoms == 1))
        {
          gl_render_atom (i);
        }
    }
}

void gl_render_atom (int i)
{
  int j;

  double line_width, atom_size;

  char string[16]="", sub_string[8]="", *labels = "ABCD";

  enum render_stereo_types get_gl_render_stereo(void);/* aro */
  GLfloat red, green, blue;

  /* For tubes mode, we don't draw an atom (a) when bonds are not
     displayed and the atom is bonded and (b) when atoms are not
     displayed and the atom is not bonded. In essense, atoms and bonds
     are mutually exclusive. */

  if ((gl_current_render_type == TUBES) && gl_use_lighting)
    {
      if (((bond_flag == 0) && (bond_adjacency_list[i] != NULL)) ||
          ((atom_flag == 0) && (bond_adjacency_list[i] == NULL))) return;
    }

  red   = atoms[i].red   / 65536.0;
  blue  = atoms[i].blue  / 65536.0;
  green = atoms[i].green / 65536.0;

  /* aro - We need to convert the colors to grayscale in order for 
     red and blue color masks to work properly */
  if ((get_gl_render_stereo() == RED_BLUE) && depth_is_on)
    {
      red = (red + blue + green) / 3;
      blue = red;
      green = red;
    }

  glPushMatrix ();
  glTranslatef (atoms[i].x, atoms[i].y, atoms[i].z);

  if ((! mouse_motion_p ()) || (! outline_mode_p ()))
    {
      glColor3f (red, green, blue);
    }
  else
    {
      glColor3f (0.0, 0.0, 0.0);
    }

  atom_size = gl_get_rendered_atom_size (i);
  line_width = 0.1 * get_atom_scale () * get_smallest_cov_rad ();

  /* Set up rendering for elliptical atoms */

  if (((gl_current_render_type == BALL_AND_STICK) ||
       ((gl_current_render_type == TUBES) && bond_adjacency_list[i] == NULL)) &&
      (atoms[i].is_ellipse == 1))
    {
      void euler_to_matrix (double *, double *);

      int j, k;

      double axis[3], angle, matrix[9];

      double get_angle_axis (double *);

      /* Apply the rotation for the orientation of the system */

      for (j = 0; j < 3; j++)
        {
          for (k = 0; k < 3; k++)
            {
              angle_axis_matrix[j][k] = global_matrix[j][k];
            }
        }

      angle = get_angle_axis (axis);
      glRotatef (-angle / DEG2RAD, axis[0], axis[1], axis[2]);

      /* Apply the rotation describing the original orientation of
         the ellipse */

      euler_to_matrix (atoms[i].euler, matrix);

      for (j = 0; j < 3; j++)
        {
          for (k = 0; k < 3; k++)
            {
              angle_axis_matrix[j][k] = matrix[(j * 3) + k];
            }
        } 

      angle = get_angle_axis (axis);
      glRotatef (angle / DEG2RAD, axis[0], axis[1], axis[2]);

      /* Apply the anisotropic scaling */

      glScalef (atoms[i].shape[0], atoms[i].shape[1], atoms[i].shape[2]);
       
    }

  if ((! mouse_motion_p ()) || (! outline_mode_p ()))
    {
      /* Draw border for selected atoms */

      if (atoms[i].sel == 1)
        {
          glColor3f (1.0, 0.65, 0.0); /* Orange */

          glDisable (GL_LIGHTING);

          gluDisk (gl_quadric,
                   atom_size - line_width,
                   atom_size,
                   gl_no_atom_segments,
                   1);

          if (gl_use_lighting) glEnable (GL_LIGHTING);

          glColor3f (red, green, blue);
        }

      if (gl_use_lighting)
        {
          double size;

          if (atoms[i].sel == 0)
            {
              size = atom_size;
            }
          else
            {
              size = atom_size - line_width;
            }

          gluSphere (gl_quadric,
                     size,
                     gl_no_atom_segments,
                     gl_no_atom_segments);
        }
      else
        {
          gluDisk (gl_quadric,
                   0,
                   atom_size,
                   gl_no_atom_segments,
                   1);

          if (atoms[i].sel == 0)
            {
              glColor3f (0.0, 0.0, 0.0);

              /* Move slightly forward */

              glTranslatef (0.0, 0.0, 0.01);

              gluDisk (gl_quadric,
                       atom_size - line_width,
                       atom_size,
                       gl_no_atom_segments,
                       1);
            }
        }
    }
  else
    {
      gluDisk (gl_quadric,
               atom_size,       /* Inner radius */
               atom_size,       /* Outer radius */
               8,               /* Small number for slices */
               1);              /* Number of rings */
    }

  if (atom_flag == 1)
    {
      /* Atom numbers */

      if (at_nos_flag)
        {
          sprintf (string, "%d ", i + 1);
        }

      /* Atom symbols */

      if (at_sym_flag)
        {
          sprintf (sub_string, "%s ", atoms[i].label);
          strcat (string, sub_string);
        }

      /* Atom selections */

      for (j = 0; j < 4; j++)
        {
          if(i == selected[j])
            {
              sprintf (sub_string, "%c ", labels[j]);
              strcat (string, sub_string);
            }
        }

      if (strlen (string) > 0)
        {
          glColor3f (0.0, 0.0, 0.0);
          glRasterPos3f (0, 0, atom_size * 1.1);

          for (j = 0; j < strlen (string); j++)
            {
              glutBitmapCharacter (GLUT_BITMAP_HELVETICA_12, string[j]);
            }

          glRasterPos3f (0, 0, atom_size * 1.1);
          gl2psText(string, "Helvetica", 12);
        }
    }

  glPopMatrix ();
}


void gl_render_bbox (void)
{
  GLfloat line_width = 2.0;

  glColor3f (0.7, 0.7, 0.7);
  glDepthMask (GL_FALSE);

  gl_render_bbox_face (0, 1, 3, 2);
  gl_render_bbox_face (4, 5, 7, 6);
  gl_render_bbox_face (0, 2, 6, 4);
  gl_render_bbox_face (1, 3, 7, 5);
  gl_render_bbox_face (0, 1, 5, 4);
  gl_render_bbox_face (2, 3, 7, 6);

  glDepthMask (GL_TRUE);

  glColor3f (0.0, 0.0, 0.0);
  glLineWidth (line_width);

  gl_render_bbox_line (0, 1);
  gl_render_bbox_line (0, 2);
  gl_render_bbox_line (0, 4);
  gl_render_bbox_line (1, 3);
  gl_render_bbox_line (1, 5);
  gl_render_bbox_line (2, 3);
  gl_render_bbox_line (2, 6);
  gl_render_bbox_line (3, 7);
  gl_render_bbox_line (4, 5);
  gl_render_bbox_line (4, 6);
  gl_render_bbox_line (5, 7);
  gl_render_bbox_line (6, 7);
}


void gl_render_bbox_line (int i, int j)
{
  glBegin (GL_LINES);
  glVertex3f (bbox.v[i][0], bbox.v[i][1], bbox.v[i][2]);
  glVertex3f (bbox.v[j][0], bbox.v[j][1], bbox.v[j][2]);
  glEnd ();
}

void gl_render_bbox_face (int i, int j, int k, int l)
{
  glBegin (GL_POLYGON);
  glVertex3f (bbox.v[i][0], bbox.v[i][1], bbox.v[i][2]);
  glVertex3f (bbox.v[j][0], bbox.v[j][1], bbox.v[j][2]);
  glVertex3f (bbox.v[k][0], bbox.v[k][1], bbox.v[k][2]);
  glVertex3f (bbox.v[l][0], bbox.v[l][1], bbox.v[l][2]);
  glEnd();
}


void gl_render_bonds (void)
{
  void gl_render_bond (int, int);

  int i, j;
  struct node *ptr;

  if (bond_flag == 0) return;

  for (i = 0; i < no_atoms; i++)
    {      

      ptr = bond_adjacency_list[i];

      while (ptr != NULL)
        {
          j = (ptr->v);

          if (atoms[i].visi && atoms[j].visi)
            {
              gl_render_bond (i, j);
            }

          ptr = ptr->next;
        }
    }
}

void gl_render_bond (int i,
                     int j)
{
  enum render_stereo_types get_gl_render_stereo(void);/* aro */
  void update_canvas_bond_points (int, int, Boolean, double);

  int k;
  double angle, b[3], bond_radius, length, mod_b, line_width;
  GLfloat red, green, blue;

  /* We want the radius, ie half the width which represents the
     diameter */

  bond_radius = get_bond_width () / 2.0;

  /* b is the vector along the bond */

  b[0] = atoms[j].x - atoms[i].x;
  b[1] = atoms[j].y - atoms[i].y;
  b[2] = atoms[j].z - atoms[i].z;
          
  mod_b = mod_of_vec (b);
  normalize_vec (b);

  angle = acos (b[2]) / DEG2RAD;

  red   = atoms[i].red   / 65536.0;
  blue  = atoms[i].blue  / 65536.0;
  green = atoms[i].green / 65536.0;

  /* aro - We need to convert the colors to grayscale in order for 
     red and blue color masks to work properly */
  if ((get_gl_render_stereo() == RED_BLUE) && depth_is_on)
    {
      red = (red + blue + green) / 3;
      blue = red;
      green = red;
    }
  
  glColorMaterial (GL_FRONT, GL_DIFFUSE);
  glColor3f (red, green, blue);

  if (gl_use_lighting)
    {
      glPushMatrix ();

      glTranslatef (atoms[i].x, atoms[i].y, atoms[i].z);

      glRotatef (180.0, 0.0, 0.0, 1.0);
      glRotatef (angle, b[1], -1.0 * b[0], 0.0);

      /* length of bond segment */

      length = (mod_b * atoms[i].cov_rad /
            (atoms[i].cov_rad + atoms[j].cov_rad));
      
      gluCylinder (gl_quadric,
                   bond_radius,
                   bond_radius,
                   length,
                   gl_no_bond_segments,
                   1);

      glPopMatrix ();
    }
  else
    {
      /* Outline */

      update_canvas_bond_points (i, j, False, 0.0);

      glColor3f (0.0, 0.0, 0.0);

      glPushMatrix ();

      glTranslatef (0.0, 0.0, -0.01);

      glBegin (GL_POLYGON);

      k = 0;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      k = 1;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      k = 4;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      k = 5;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      glEnd ();

      glPopMatrix ();

      /* Colour blocks */

      glColor3f (red, green, blue);

      line_width = 0.1 * get_bond_scale () * get_smallest_cov_rad ();
      
      update_canvas_bond_points (i, j, False, line_width);

      glBegin (GL_POLYGON);

      k = 0;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      k = 1;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      k = 4;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      k = 5;

      glVertex3f (cartesian_bond_points[k][0],
                  cartesian_bond_points[k][1],
                  cartesian_bond_points[k][2]);

      glEnd ();

    }
}

void gl_render_hbonds (void)
{

  void gl_render_hbond (int, int);

  int i, j;
  struct node *ptr;

  if ((hbond_flag == 0) || (any_hydrogen == 0)) return;

  for (i = 0; i < no_atoms; i++)
    {      

      ptr = hbond_adjacency_list[i];

      while (ptr != NULL)
        {
          j = (ptr->v);

          if (i > j)           /* don't do things twice */
            {
              if (atoms[i].visi && atoms[j].visi)
                {
                  gl_render_hbond (i, j);
                }
            }

          ptr = ptr->next;
            
        }
    }
}


void gl_render_hbond (int i,
                      int j)
{
  int k, l, no_segments = 5;
  double angle, atom_size, bond_length, segment_length, hbond_width;
  double start[3], end[3], bond_vector[3], segment[3];

  /* The whole bond */

  bond_vector[0] = atoms[j].x - atoms[i].x;
  bond_vector[1] = atoms[j].y - atoms[i].y;
  bond_vector[2] = atoms[j].z - atoms[i].z;

  normalize_vec (bond_vector);

  /* The visible portion of the bond */

  atom_size = gl_get_rendered_atom_size (i);

  start[0] = atoms[i].x + (atom_size * bond_vector[0]);
  start[1] = atoms[i].y + (atom_size * bond_vector[1]);
  start[2] = atoms[i].z + (atom_size * bond_vector[2]);

  atom_size = gl_get_rendered_atom_size (j);

  end[0] = atoms[j].x - (atom_size * bond_vector[0]);
  end[1] = atoms[j].y - (atom_size * bond_vector[1]);
  end[2] = atoms[j].z - (atom_size * bond_vector[2]);

  for (k = 0; k < 3; k++)
    {
      bond_vector[k] = end[k] - start[k];
    }

  bond_length = mod_of_vec (bond_vector);
  segment_length = bond_length / ((2 * no_segments) - 1);

  normalize_vec (bond_vector);
  angle = acos (bond_vector[2]) / DEG2RAD;

  glColorMaterial (GL_FRONT, GL_DIFFUSE);
  /* aro - as long as hbonds are always grayscale, they'll
     render properly in red/blue stereo mode */
  glColor3f (0.0, 0.0, 0.0);

  for (k = 0; k < no_segments; k++)
    {
      /* Coordinate for start of segment */

      for (l = 0; l < 3; l++)
        {
          segment[l] = start[l] + (k * 2) * bond_vector[l] * segment_length;
        }

      glPushMatrix ();
      glTranslatef (segment[0], segment[1], segment[2]);

      glRotatef (180.0, 0.0, 0.0, 1.0);
      glRotatef (angle, bond_vector[1], -1.0 * bond_vector[0], 0.0);

      /* length of bond segment */

      hbond_width = 0.15 * get_hbond_scale ();

      gluCylinder (gl_quadric,
                   hbond_width,
                   hbond_width,
                   segment_length,
                   gl_no_bond_segments,
                   1);

      glPopMatrix ();

    }
}

double gl_get_rendered_atom_size (int i)
{
  int get_gl_render_type (void);
  double atom_size = 0;

  /* Work out the size the atom is going to be rendered at taking into
     account the drawing mode (BALL_AND_STICK or TUBES) and whether or
     not atoms and bonds are being displayed */

  /* We need this for bounding box calculation; it is a duplication of
     effort when called from gl_render_atom. */

  gl_current_render_type = (atoms[i].gl_render_type == DEFAULT) ?
    get_gl_render_type () : atoms[i].gl_render_type;

  if ((gl_current_render_type == BALL_AND_STICK) || (! gl_use_lighting))
    {
      if (atom_flag)
        {
          /* If not bonded use VDW, otherwise covalent radius */

          if ((bond_adjacency_list[i] == NULL) && (never_use_vdw == 0))
            {
              atom_size = get_atom_scale () * atoms[i].vdw_rad;
            }
          else
            {
              atom_size = get_atom_scale () * atoms[i].cov_rad;
            }
        }
      else
        {
          atom_size = 0;        /* Atoms not being rendered */
        }
    }
  else if (gl_current_render_type == TUBES)
    {
      if (bond_adjacency_list[i] == NULL)
        {
          /* Atom is not bonded; only render if atoms being displayed */

          if (atom_flag)
            {
              if (never_use_vdw == 1)
                {
                  atom_size = get_atom_scale () * atoms[i].cov_rad;
                }
              else
                {
                  atom_size = get_atom_scale () * atoms[i].vdw_rad;
                }
            }
          else
            {
              atom_size = 0;
            }
        }
      else
        {
          /* Atom is bonded; only render if bonds are being displayed */

          if (bond_flag)
            {
              atom_size = get_bond_width () / 2;
            }
          else
            {
              atom_size = 0;
            }
        }
    }

  return (atom_size);

}

void gl_render_vectors (void)
{

  void gl_render_vector (int, int);

  int i, j;

  if (vector_flag == 0)
    {
      return;
    }

  glColorMaterial (GL_FRONT, GL_DIFFUSE);

  for (i = 0; i < no_atoms; i++)
    {
      for (j = 0; j < MAX_VECTORS_PER_ATOM; j++)
        {
          if (atoms[i].has_vector > j)
            {
              gl_render_vector (i, j);
            }
        }
    }
}

void gl_render_vector (int i, int j)
{

  double get_vector_arrow_angle (void);
  double get_vector_arrow_scale (void);
  double get_vector_scale (void);

  double angle, height, mod_of_vector, top_radius;
  double vector_arrow_angle, vector_arrow_scale, vector_scale;
  double vector[3];
  double atom_size = gl_get_rendered_atom_size (i);

  vector_arrow_angle = get_vector_arrow_angle ();
  vector_arrow_scale = get_vector_arrow_scale ();
  vector_scale = get_vector_scale ();

  /* Draw stem */
  glColor3f (0.5, 0.5, 0.5); 

  vector[0] = atoms[i].v[j][0] * vector_scale;
  vector[1] = atoms[i].v[j][1] * vector_scale;
  vector[2] = atoms[i].v[j][2] * vector_scale;

  mod_of_vector = mod_of_vec (vector);

  normalize_vec (vector);

  height = mod_of_vector * vector_arrow_scale;

  angle = acos (vector[2]) / DEG2RAD;

  glPushMatrix ();

  glTranslatef (atoms[i].x, atoms[i].y, atoms[i].z);
  glRotatef (180.0, 0.0, 0.0, 1.0);
  glRotatef (angle, vector[1], -1.0 * vector[0], 0.0);

  glTranslatef (0.0, 0.0, atom_size);

  gluCylinder (gl_quadric,
               0.025,
               0.025,
               mod_of_vector - (height + atom_size),
               16,
               1);

  glPopMatrix ();

  /* Draw head  */

  glColor3f (0.0, 0.0, 0.0);

  top_radius = height *
    tan (vector_arrow_angle * DEG2RAD);

  glPushMatrix ();

  glTranslatef (atoms[i].x + (mod_of_vector - height) * vector[0],
                atoms[i].y + (mod_of_vector - height) * vector[1],
                atoms[i].z + (mod_of_vector - height) * vector[2]);
  glRotatef (180.0, 0.0, 0.0, -1.0);
  glRotatef (angle, vector[1], -1.0 * vector[0], 0.0);

  gluCylinder (gl_quadric,
               top_radius,
               0,           /* base radius */
               height,
               16,          /* slices */
               8);          /* stacks */

  glPopMatrix ();

}


void gl_init (void)
{
  void bg_color_init(void); /* aro */

  XColor get_bg_color (void);

  double red, green, blue;

  Boolean color_initialized = False;

  static XColor bg_color_rgb;

  GLfloat light_position[] = {0.0, 0.0, 1.0, 0.0};

  if (color_initialized == False)
    {
      if(bg_color_parsed == 0 && strcmp(bg_color,"") != 0){
        bg_color_init();
        bg_color_parsed = 1;
      }

      bg_color_rgb = get_bg_color ();

      red   = bg_color_rgb.red   / 65535.0;
      green = bg_color_rgb.green / 65535.0;
      blue  = bg_color_rgb.blue  / 65535.0;
    }

  glClearColor (red, green, blue, 0.0);

  glShadeModel (GL_SMOOTH);

  /* Lighting */

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glLightfv (GL_LIGHT0, GL_POSITION, light_position);
  glEnable (GL_LIGHT0);
  glDepthFunc (GL_LESS);

  glEnable (GL_COLOR_MATERIAL);
 
  /* Allocate a quadric */

  if (gl_quadric == NULL)
    {
      gl_quadric = gluNewQuadric ();
    }
}

void gl_set_perspective (void)
{

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gl_update_near_and_far ();

  if (depth_is_on)
    {
      gluPerspective (gl_fov, 1.0, gl_perspective_near, gl_perspective_far);
    }
  else
    {
      double h;

      /* Use the field of view angle (gl_fov) to get the dimensions of
         the rendered scene (h) at the origin; this is a square gl_eye
         from the viewpoint */

      h = gl_eye * tan (gl_fov * PI / 360.0);

      glOrtho (-h, h, -h, h, gl_perspective_near, gl_perspective_far);
    }
}

void gl_update_near_and_far (void)
{
  double get_vector_scale (void);

  int i, j;
  
  double vector_scale = get_vector_scale ();
  double outline_offset = 0.1, shape_scale;
  Boolean max_set = False, min_set = False;

  /* Get the atoms with minimum/maximum Z for gluPerspective */

  for (i = 0; i < no_atoms; i++)
    {

      /* Take account of elliptical atoms */

      if (atoms[i].is_ellipse == 1)
        {
          shape_scale = atoms[i].max_shape;
        }
      else
        {
          shape_scale = 1.0;
        }

      /* Maximum distance of atom from origin; take in account the
         maximum radius of the atom since we don't want to clip it in
         the centre */

      if (max_set)
        {
          if ((atoms[i].z + (shape_scale * atoms[i].vdw_rad)) > gl_max_z)
            {
              gl_max_z = atoms[i].z + (shape_scale * atoms[i].vdw_rad);
            }
        }
      else
        {
          gl_max_z = atoms[i].z + (shape_scale * atoms[i].vdw_rad);
          max_set = True;
        }

      /* We use outline_offset since when in outline mode, the disk
         furthest from the eye disappears if some leeway isn't set */

      if (min_set)
        {
          if ((atoms[i].z - (shape_scale * atoms[i].vdw_rad)) < gl_min_z)
            {
              gl_min_z = (atoms[i].z - (shape_scale * atoms[i].vdw_rad)) - outline_offset;
            }
        }
      else
        {
          gl_min_z = (atoms[i].z - (shape_scale * atoms[i].vdw_rad)) - outline_offset;
          min_set = True;
        }
    }

  /* Now check the vectors */

  if (vector_flag == 1)
    {
      for (i = 0; i < no_atoms; i++)
        {
          for (j = 0; j < MAX_VECTORS_PER_ATOM; j++)
            {
              if (atoms[i].has_vector > j)
                {
                  if ((atoms[i].z + (atoms[i].v[j][2] * vector_scale)) > gl_max_z)
                    {
                      gl_max_z = atoms[i].z + (atoms[i].v[j][2] * vector_scale);
                    }
                  else if ((atoms[i].z + (atoms[i].v[j][2] * vector_scale)) < gl_min_z)
                    {
                      gl_min_z = atoms[i].z + (atoms[i].v[j][2] * vector_scale);
                    }
                }
            }
        }

      /* FIX ME -- we haven't taken into account the vector head,
         which may be clipped; fudge this */

      {
        double max_vector_size = 10.0;

        gl_max_z += max_vector_size;
        gl_min_z -= max_vector_size;
      }

    }

 
  if (gl_eye > gl_max_z)
    {
      gl_perspective_near = gl_eye - gl_max_z;
    }
  else
    {
      gl_perspective_near = 0.01; /* Small and positive */
    }

  if (gl_eye > gl_min_z)
    {
      gl_perspective_far = gl_eye - gl_min_z;
    }
  else
    {
      gl_perspective_far = 0.02; /* Small and slightly more positive */
    }

}

int get_gl_no_atom_segments (void)
{
  return (gl_no_atom_segments);
}

void set_gl_no_atom_segments (int new_value)
{
  gl_no_atom_segments = new_value;
}

int get_gl_no_bond_segments (void)
{
  return (gl_no_bond_segments);
}

void set_gl_no_bond_segments (int new_value)
{
  gl_no_bond_segments = new_value;
}

double get_gl_fov (void)
{
  return (gl_fov);
}

void set_gl_fov (double new_value)
{
  gl_fov = new_value;

  gl_set_perspective ();
}

double get_gl_eye (void)
{
  return (gl_eye);
}

void set_gl_eye (double new_value)
{
  gl_eye = new_value;

  gl_set_perspective ();
}

/* aro--> */

int get_gl_sep (void)
{
  return (gl_sep);
}

void set_gl_sep (int new_value)
{
  gl_sep = new_value;
}

/* Logical variable used to determine if GL rendering is active */

static Boolean render_using_gl = True;

void set_render_using_gl (Boolean value)
{
  render_using_gl = value;
}

Boolean render_using_gl_p (void)
{
  return (render_using_gl);
}

int get_gl_render_type (void)
{
  return (gl_render_type);
}

void set_gl_render_type (enum gl_render_types new_value)
{
  gl_render_type = new_value;
}

/* aro--> */

/* Variable used to determine whether stereo rendering is active */
static enum render_stereo_types render_stereo_mode = NO_STEREO;

void set_gl_render_stereo(enum render_stereo_types value)
{

#ifdef SGI_STEREO
  int get_monitor_frequency();
  void set_monitor_frequency(int);
  Boolean render_using_gl_p();
  
  static Dimension w;
  static Dimension h;
  Dimension new_w;
  static int refresh_rate;
  int event, error;
  char* display_env;
#endif /* SGI_STEREO */
  
  render_stereo_mode = value;

#ifdef SGI_STEREO
  if(value == SGI_HARDWARE && render_using_gl_p())
    {
      display_env = getenv("DISPLAY");

      if(display_env[0] != ':') 
        {
          fprintf(stderr, "Not running on a local display, can't change to SGI stereo mode\n");
        }
      else if(XSGIStereoQueryExtension(display, &event, &error) == False) 
        {
          fprintf(stderr, "SGI Stereo Extensions not supported by X server\n");
        } 
      else if(XSGIQueryStereoMode(display, XtWindow(canvas)) == X_STEREO_UNSUPPORTED) 
        {
          fprintf(stderr, "SGI Stereo Extensions not supported by current screen\n");
        } 
      else if(XSGIQueryStereoMode(display, XtWindow(canvas)) != STEREO_TOP)
        {
          XtVaSetValues(toplevel, XmNallowShellResize, True, NULL);
          XtVaGetValues(canvas, XmNwidth, &w, NULL);
          XtVaGetValues(canvas, XmNheight, &h, NULL);
          new_w = w / 0.48;
          XtVaSetValues(canvas, XmNwidth, new_w, NULL);
          /* Hard coded 392 for now, not sure if this is uniform 
             resolution across SGI's when in STR_TOP mode */
          XtVaSetValues(canvas, XmNheight, 392, NULL);
          XtVaSetValues(toplevel, XmNallowShellResize, False, NULL);
          
          refresh_rate = get_monitor_frequency();

          system("/usr/gfx/setmon -n STR_TOP");
        }
      
    }
  else if(XSGIQueryStereoMode(display, XtWindow(canvas)) == STEREO_TOP)
    {
      set_monitor_frequency(refresh_rate); /*makes system call to setmon */
      
      XtVaSetValues(toplevel, XmNallowShellResize, True, NULL);
      XtVaSetValues(canvas, XmNwidth, w, NULL);
      XtVaSetValues(canvas, XmNheight, h, NULL);
      XtVaSetValues(toplevel, XmNallowShellResize, False, NULL);
      
    }
  
#endif /* SGI_STEREO */

}

enum render_stereo_types get_gl_render_stereo()
{
  return render_stereo_mode;
}
/* <--aro */

void gl_set_lighting (int new_value)
{
  gl_use_lighting = new_value;
}

int gl_get_lighting (void)
{
  return (gl_use_lighting);
}

void gl_set_specular_lighting (int new_value)
{
  GLfloat specular[] = {0.5, 0.5, 0.5, 1.0}; /* Configurable? */
  GLfloat no_specular[] = {0.0, 0.0, 0.0, 1.0};

  gl_use_specular_lighting = new_value;

  if (gl_use_specular_lighting)
    {
      glMaterialfv (GL_FRONT, GL_SPECULAR, specular);
      glMaterialfv (GL_FRONT, GL_SHININESS, gl_shininess);
    }
  else
    {
      glMaterialfv (GL_FRONT, GL_SPECULAR, no_specular);
      glMaterialfv (GL_FRONT, GL_SHININESS, gl_shininess);
    }
}

int gl_get_specular_lighting (void)
{
  return (gl_use_specular_lighting);
}


void gl_set_shininess (int new_value)
{
  GLfloat specular[] = {0.5, 0.5, 0.5, 1.0}; /* Configurable? */
  GLfloat no_specular[] = {0.0, 0.0, 0.0, 1.0};

  gl_shininess[0] = new_value;

  if (gl_use_specular_lighting)
    {
      glMaterialfv (GL_FRONT, GL_SPECULAR, specular);
      glMaterialfv (GL_FRONT, GL_SHININESS, gl_shininess);
    }
  else
    {
      glMaterialfv (GL_FRONT, GL_SPECULAR, no_specular);
      glMaterialfv (GL_FRONT, GL_SHININESS, gl_shininess);
    }
}

int gl_get_shininess (void)
{
  return (gl_shininess[0]);
}

void set_gl_copy_canvas (int value)
{
  gl_copy_canvas = value;
}

#endif /* GL */
