#ifndef __ARCH_MMU_BITS_H
#define __ARCH_MMU_BITS_H

#ifdef ASSEMBLY
#define BM(base, count, val) (((val) & ((0x1 << (count)) - 1)) << (base))
#else
#define BM(base, count, val) (((val) & ((1ull << (count)) - 1)) << (base))
#endif
/* L0/L1/L2/L3 descriptor types */
#define MMU_PTE_DESCRIPTOR_INVALID              BM(0, 2, 0)
#define MMU_PTE_DESCRIPTOR_MASK                 BM(0, 2, 3)

/* L0/L1/L2 descriptor types */
#define MMU_PTE_L012_DESCRIPTOR_BLOCK           BM(0, 2, 1)
#define MMU_PTE_L012_DESCRIPTOR_TABLE           BM(0, 2, 3)

/* L3 descriptor types */
#define MMU_PTE_L3_DESCRIPTOR_PAGE              BM(0, 2, 3)

/* Output address mask */
#define MMU_PTE_OUTPUT_ADDR_MASK                BM(12, 36, 0xfffffffff)

/* Table attrs */
#define MMU_PTE_ATTR_NS_TABLE                   BM(63, 1, 1)
#define MMU_PTE_ATTR_AP_TABLE_NO_WRITE          BM(62, 1, 1)
#define MMU_PTE_ATTR_AP_TABLE_NO_EL0            BM(61, 1, 1)
#define MMU_PTE_ATTR_UXN_TABLE                  BM(60, 1, 1)
#define MMU_PTE_ATTR_PXN_TABLE                  BM(59, 1, 1)

/* Block/Page attrs */
#define MMU_PTE_ATTR_RES_SOFTWARE               BM(55, 4, 0xf)
#define MMU_PTE_ATTR_UXN                        BM(54, 1, 1)
#define MMU_PTE_ATTR_PXN                        BM(53, 1, 1)
#define MMU_PTE_ATTR_CONTIGUOUS                 BM(52, 1, 1)

#define MMU_PTE_ATTR_NON_GLOBAL                 BM(11, 1, 1)
#define MMU_PTE_ATTR_AF                         BM(10, 1, 1)

#define MMU_PTE_ATTR_SH_NON_SHAREABLE           BM(8, 2, 0)
#define MMU_PTE_ATTR_SH_OUTER_SHAREABLE         BM(8, 2, 2)
#define MMU_PTE_ATTR_SH_INNER_SHAREABLE         BM(8, 2, 3)


#define MMU_PTE_ATTR_AP_P_RW_U_NA               BM(6, 2, 0)
#define MMU_PTE_ATTR_AP_P_RW_U_RW               BM(6, 2, 1)
#define MMU_PTE_ATTR_AP_P_RO_U_NA               BM(6, 2, 2)
#define MMU_PTE_ATTR_AP_P_RO_U_RO               BM(6, 2, 3)
#define MMU_PTE_ATTR_AP_MASK 0xC0


#define MMU_PTE_ATTR_ATTR_INDEX(attrindex)      BM(2, 3, attrindex)
#define MMU_PTE_ATTR_ATTR_INDEX_MASK            MMU_PTE_ATTR_ATTR_INDEX(7)


#define MMU_MAIR_ATTR(index, attr)              BM(index * 8, 8, (attr))

#define MMU_MAIR_ATTR0                  MMU_MAIR_ATTR(0, 0x00)
#define MMU_PTE_ATTR_STRONGLY_ORDERED   MMU_PTE_ATTR_ATTR_INDEX(0)

/* Device-nGnRE memory */
#define MMU_MAIR_ATTR1                  MMU_MAIR_ATTR(1, 0x04)
#define MMU_PTE_ATTR_DEVICE             MMU_PTE_ATTR_ATTR_INDEX(1)


#define MMU_MAIR_ATTR2                  MMU_MAIR_ATTR(2, 0xff)
#define MMU_PTE_ATTR_NORMAL_MEMORY      MMU_PTE_ATTR_ATTR_INDEX(2)

#define MMU_MAIR_ATTR3                  (0)
#define MMU_MAIR_ATTR4                  (0)
#define MMU_MAIR_ATTR5                  (0)
#define MMU_MAIR_ATTR6                  (0)
#define MMU_MAIR_ATTR7                  (0)

#define MMU_MAIR_VAL                    (MMU_MAIR_ATTR0 | MMU_MAIR_ATTR1 | \
		                                         MMU_MAIR_ATTR2 | MMU_MAIR_ATTR3 | \
		                                         MMU_MAIR_ATTR4 | MMU_MAIR_ATTR5 | \
		                                         MMU_MAIR_ATTR6 | MMU_MAIR_ATTR7 )

#define MMU_PTE_ATTR_VALID 1

#define MMU_PTE_IDENT_FLAGS \
	    (MMU_PTE_L3_DESCRIPTOR_PAGE | \
	          MMU_PTE_ATTR_AF | \
	          MMU_PTE_ATTR_SH_INNER_SHAREABLE | \
	          MMU_PTE_ATTR_NORMAL_MEMORY | \
	          MMU_PTE_ATTR_AP_P_RW_U_NA)

#define MMU_PTE_KERNEL_RO_FLAGS \
	    (MMU_PTE_ATTR_UXN | \
	          MMU_PTE_ATTR_AF | \
	          MMU_PTE_ATTR_SH_INNER_SHAREABLE | \
	          MMU_PTE_ATTR_NORMAL_MEMORY | \
	          MMU_PTE_ATTR_AP_P_RO_U_NA)

#define MMU_PTE_KERNEL_DATA_FLAGS \
	    (MMU_PTE_ATTR_UXN | \
	          MMU_PTE_ATTR_PXN | \
	          MMU_PTE_ATTR_AF | \
	          MMU_PTE_ATTR_SH_INNER_SHAREABLE | \
	          MMU_PTE_ATTR_NORMAL_MEMORY | \
	          MMU_PTE_ATTR_AP_P_RW_U_NA)

#define MMU_INITIAL_MAP_STRONGLY_ORDERED \
	    (MMU_PTE_ATTR_UXN | \
	          MMU_PTE_ATTR_PXN | \
	          MMU_PTE_ATTR_AF | \
	          MMU_PTE_ATTR_STRONGLY_ORDERED | \
	          MMU_PTE_ATTR_AP_P_RW_U_NA)

#define MMU_INITIAL_MAP_DEVICE \
	    (MMU_PTE_ATTR_UXN | \
	          MMU_PTE_ATTR_PXN | \
	          MMU_PTE_ATTR_AF | \
	          MMU_PTE_ATTR_DEVICE | \
	          MMU_PTE_ATTR_AP_P_RW_U_NA)

#endif

