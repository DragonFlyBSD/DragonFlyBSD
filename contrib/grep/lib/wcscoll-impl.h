/* Compare two wide strings using the current locale.
   Copyright (C) 2011-2012 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2011.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

int
wcscoll (const wchar_t *s1, const wchar_t *s2)
{
  char mbbuf1[1024];
  char mbbuf2[1024];
  char *mbs1;
  char *mbs2;

  {
    int saved_errno = errno;

    /* Convert s1 to a multibyte string, trying to avoid malloc().  */
    {
      size_t ret;

      ret = wcstombs (mbbuf1, s1, sizeof (mbbuf1));
      if (ret == (size_t)-1)
        goto failed1;
      if (ret < sizeof (mbbuf1))
        mbs1 = mbbuf1;
      else
        {
          size_t need = wcstombs (NULL, s1, 0);
          if (need == (size_t)-1)
            goto failed1;
          mbs1 = (char *) malloc (need + 1);
          if (mbs1 == NULL)
            goto out_of_memory1;
          ret = wcstombs (mbs1, s1, need + 1);
          if (ret != need)
            abort ();
        }
    }

    /* Convert s2 to a multibyte string, trying to avoid malloc().  */
    {
      size_t ret;

      ret = wcstombs (mbbuf2, s2, sizeof (mbbuf2));
      if (ret == (size_t)-1)
        goto failed2;
      if (ret < sizeof (mbbuf2))
        mbs2 = mbbuf2;
      else
        {
          size_t need = wcstombs (NULL, s2, 0);
          if (need == (size_t)-1)
            goto failed2;
          mbs2 = (char *) malloc (need + 1);
          if (mbs2 == NULL)
            goto out_of_memory2;
          ret = wcstombs (mbs2, s2, need + 1);
          if (ret != need)
            abort ();
        }
    }

    /* No error so far.  */
    errno = saved_errno;
  }

  /* Compare the two multibyte strings.  */
  {
    int result = strcoll (mbs1, mbs2);

    if (mbs1 != mbbuf1)
      {
        int saved_errno = errno;
        free (mbs1);
        errno = saved_errno;
      }
    if (mbs2 != mbbuf2)
      {
        int saved_errno = errno;
        free (mbs2);
        errno = saved_errno;
      }
    return result;
  }

 out_of_memory2:
  if (mbs1 != mbbuf1)
    free (mbs1);
 out_of_memory1:
  errno = ENOMEM;
  return 0;

 failed2:
  if (mbs1 != mbbuf1)
    free (mbs1);
 failed1:
  errno = EILSEQ;
  return 0;
}
