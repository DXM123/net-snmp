/****************************************************************************
 * Module for ucd-snmpd reading IP Firewall accounting rules.               *
 * It reads "/proc/net/ip_acct". If the file has a wrong format it silently *
 * returns erroneous data but doesn't do anything harmfull. Based (on the   *
 * output of) mib2c, wombat.c, proc.c and the Linux kernel.                 *
 * Author: Cristian.Estan@net.utcluj.ro                                     *
 ***************************************************************************/

#include <config.h>
#include "mibincl.h"
#include "ipfwacc.h"

/* According to the 2.0.33 Linux kernel, assuming we use ipv4 any line from
 * "/proc/net/ip_acct should fit into
 * 8+1+8+2+8+1+8+1+16+1+8+1+4+1+2+1+2+1+20+20+10*(1+5)+2+2+2+2=182
 * characters+ newline.
 */
#define IPFWRULELEN 200

#define IP_FW_F_ALL     0x0000  /* This is a universal packet firewall*/
#define IP_FW_F_TCP     0x0001  /* This is a TCP packet firewall      */
#define IP_FW_F_UDP     0x0002  /* This is a UDP packet firewall      */
#define IP_FW_F_ICMP    0x0003  /* This is a ICMP packet firewall     */
#define IP_FW_F_KIND    0x0003  /* Mask to isolate firewall kind      */
#define IP_FW_F_SRNG    0x0008  /* The first two src ports are a min  *
                                 * and max range (stored in host byte *
                                 * order).                            */
#define IP_FW_F_DRNG    0x0010  /* The first two dst ports are a min  *
                                 * and max range (stored in host byte *
                                 * order).                            *
                                 * (ports[0] <= port <= ports[1])     */
#define IP_FW_F_BIDIR   0x0040  /* For bidirectional firewalls        */
#define IP_FW_F_ACCTIN  0x1000  /* Account incoming packets only.     */
#define IP_FW_F_ACCTOUT 0x2000  /* Account outgoing packets only.     */

static unsigned char rule[IPFWRULELEN]; /*Buffer for reading a line from
					 * /proc/net/ip_acct. Care has been taken
					 * not to read beyond the end of this 
					 * buffer, even if rules are in an 
					 * unexpected format
					 */

/* This function reads the rule with the given number into the buffer. It
 * returns the number of rule read or 0 if the number is invalid or other
 * problems occur. If the argument is 0 it returns the number of accounting
 * rules. No caching of rules is done.
 */

static int readrule(unsigned int number)
{ 
  int i,retval;
  FILE* f= fopen("/proc/net/ip_acct","rt");

  if (!f)
    return 0;
  /*get rid of "IP accounting rules" line*/
  if (!fgets(rule,IPFWRULELEN,f))
    { fclose(f);
      return 0;
    }
  for(i=1;i!=number;i++)
    if (!fgets(rule,IPFWRULELEN,f))
      { fclose(f);
	return (number?0:(i-1));
      }
  if (!fgets(rule,IPFWRULELEN,f))
    { fclose(f);
      return 0;
    }
  fclose(f);
  return i;
}

static unsigned long ret_val; /* Used by var_ipfwacc to return ulongs */

/* This function converts the hexadecimal representation of an IP address from
 * the rule buffer to an unsigned long. The result is stored in the ret_val
 * variable. The parameter indicates the position where the address starts. It
 * only works with uppercase letters and assumes input is correct. Had to use
 * this because stol returns a signed long. 
 */

static inline void atoip (int pos)
{
  int i;

  ret_val=0;
  for (i=0;i<32;i+=8)
    { unsigned long value = (((rule[pos])>='0'&&rule[pos]<='9')?
			     rule[pos]-'0':rule[pos]-'A'+10);
      pos++;
      value= (value<<4) + (((rule[pos])>='0'&&rule[pos]<='9')?
	       rule[pos]-'0':rule[pos]-'A'+10);
      pos++;
      ret_val |= (value<<i);
    }
}

/* This function parses the flags field from the line in the buffer */

static unsigned long int getflags ()
{ 
  unsigned long int flags; 
  int i=37; /* position in the rule */

  /* skipping via name */
  while (rule[i]!=' '&&i<IPFWRULELEN-12)
    i++;
  /* skipping via address */
  i+=10;
  for (flags=0;rule[i]!=' '&&i<IPFWRULELEN-1;i++)
    { int value = (((rule[i])>='0'&&rule[i]<='9')?
		   rule[i]-'0':rule[i]-'A'+10);
      flags=(flags<<4)+value;
    }
  return flags;
}

/* This function reads into ret_val a field from the rule buffer. The field
 * is a base 10 long integer and the parameter skip tells us how many fields
 * to skip after the "via addrress" field (including the flag field)
 */

static void getnumeric(int skip)
{ 
 int i=37; /* position in the rule */
 
  /* skipping via name */
  while (rule[i]!=' '&&i<IPFWRULELEN-12)
    i++;
  /* skipping via address */
  i+=10;
  while (skip>0)
    { skip--;
    /* skipping field, than subsequent spaces */
    while (rule[i]!=' '&&i<IPFWRULELEN-2)
      i++;
    while (rule[i]==' '&&i<IPFWRULELEN-1)
      i++;
    }
  for (ret_val=0;rule[i]!=' '&& i<IPFWRULELEN-1;i++)
    ret_val=ret_val*10+rule[i]-'0';
}

unsigned char * var_ipfwacc(struct variable *vp,
			    oid *name,
			    int *length,
			    int exact,
			    int *var_len,
			    WriteMethod **write_method)    
{
  *write_method = 0;           /* assume it isnt writable for the time being */
  *var_len = sizeof(ret_val);  /* assume an integer and change later if not */

  if (!checkmib(vp,name,length,exact,var_len,write_method,readrule(0)))
	return (NULL);

  if (readrule(name[*length-1])){
    /* this is where we do the value assignments for the mib results. */
    switch(vp->magic) {
      case IPFWACCINDEX:
	ret_val = name[*length-1];
        return((u_char *) (&ret_val));
      case IPFWACCSRCADDR:
        atoip(0);
        return((u_char *) (&ret_val));
      case IPFWACCSRCNM:
        atoip(9);
        return((u_char *) (&ret_val));
      case IPFWACCDSTADDR:
        atoip(19);
        return((u_char *) (&ret_val));
      case IPFWACCDSTNM:
        atoip(28);
        return((u_char *) (&ret_val));
      case IPFWACCVIANAME:
	{ int i=37; /* position in the rule */
	  while (rule[i]!=' '&&i<IPFWRULELEN-1)
	    i++;
	  rule[i]=0;
	  return (rule+37);
	}
      case IPFWACCVIAADDR:
	{ int i=37; /* position in the rule */
	  while (rule[i]!=' '&&i<IPFWRULELEN-9)
	    i++;
	  atoip(i+1);
	  return((u_char *) (&ret_val));
	}
      case IPFWACCPROTO:
	switch (getflags()&IP_FW_F_KIND){
	case IP_FW_F_ALL: ret_val=2; return ((u_char *) (&ret_val));
	case IP_FW_F_TCP: ret_val=3; return ((u_char *) (&ret_val));
	case IP_FW_F_UDP: ret_val=4; return ((u_char *) (&ret_val));
	case IP_FW_F_ICMP: ret_val=5; return ((u_char *) (&ret_val));
	default: ret_val=1; return((u_char *) (&ret_val));
	}
      case IPFWACCBIDIR:
	ret_val=((getflags()&IP_FW_F_BIDIR)?2:1);
	return ((u_char *) (&ret_val));
      case IPFWACCDIR:
	ret_val=(getflags()&(IP_FW_F_ACCTIN|IP_FW_F_ACCTOUT));
        if (ret_val==IP_FW_F_ACCTIN)
	  ret_val=2;
	else if (ret_val==IP_FW_F_ACCTOUT)
	  ret_val=3;
	else
	  ret_val=1;
	return ((u_char *) (&ret_val));
      case IPFWACCBYTES:
	getnumeric(4);
	return ((u_char *) (&ret_val));
      case IPFWACCPACKETS:
	getnumeric(3);
        return ((u_char *) (&ret_val));
      case IPFWACCNSRCPRTS:
	getnumeric(1);
        return ((u_char *) (&ret_val));
      case IPFWACCNDSTPRTS:
	getnumeric(2);
        return ((u_char *) (&ret_val));
      case IPFWACCSRCISRNG:
	ret_val=((getflags()&IP_FW_F_SRNG)?1:2);
	return ((u_char *) (&ret_val));
      case IPFWACCDSTISRNG:
	ret_val=((getflags()&IP_FW_F_DRNG)?1:2);
	return ((u_char *) (&ret_val));
      case IPFWACCPORT1:
      case IPFWACCPORT2:
      case IPFWACCPORT3:
      case IPFWACCPORT4:
      case IPFWACCPORT5:
      case IPFWACCPORT6:
      case IPFWACCPORT7:
      case IPFWACCPORT8:
      case IPFWACCPORT9:
      case IPFWACCPORT10:
	getnumeric(5+(vp->magic)-IPFWACCPORT1);
        return ((u_char *) (&ret_val));
    }
  }
  return NULL;
}
