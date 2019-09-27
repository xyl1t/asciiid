#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "mesh.h"
#include "matrix.h"

#include "terrain.h"

struct Line;
struct Face;

struct Vert
{
    Mesh* mesh;

    // in mesh
	Vert* next;
	Vert* prev;

	Face* face_list;
	Line* line_list;

    // regardless of mesh type (2d/3d)
    // we keep z (makes it easier to switch mesh types back and forth)
	float xyzw[4];
	unsigned char rgba[4];

    bool sel;
};

struct Face
{
    Mesh* mesh;

    // in mesh
	Face* next;
	Face* prev;

	Vert* abc[3];
	Face* share_next[3]; // next triangle sharing given vertex

	uint32_t visual; // matid_8bits, 3 x {shade_7bits,elev_1bit}

    bool sel;
};


struct Line
{
    Mesh* mesh;

    // in mesh
	Line* next;
	Line* prev;

	Vert* ab[2];
	Line* share_next[2]; // next line sharing given vertex

	uint32_t visual; // line style & height(depth) offset?

    bool sel;
};

struct Inst;
struct World;
struct Mesh
{
    World* world;
    char* name; // in form of library path?
    void* cookie;

    // in world
	Mesh* next;
	Mesh* prev;

    enum TYPE
    {
        MESH_TYPE_2D,
        MESH_TYPE_3D
    };

    TYPE type;

	int faces;
	Face* head_face;
	Face* tail_face;

	int lines;
	Line* head_line;
	Line* tail_line;

	int verts;
	Vert* head_vert;
	Vert* tail_vert;

    // untransformed bbox
    float bbox[6];

    Inst* share_list;

    bool Update(const char* path);
};

struct BSP
{
    enum TYPE
    {
        BSP_TYPE_NODE,
        BSP_TYPE_NODE_SHARE,
        BSP_TYPE_LEAF,
        BSP_TYPE_INST
    };

    TYPE type;    
    float bbox[6]; // in world coords
    BSP* bsp_parent; // BSP_Node or BSP_NodeShare or NULL if not attached to tree
};

struct BSP_Node : BSP
{
    BSP* bsp_child[2];
};

struct BSP_NodeShare : BSP_Node
{
    // list of shared instances 
    Inst* head; 
    Inst* tail;
};

struct BSP_Leaf : BSP
{
    // list of instances 
   Inst* head; 
   Inst* tail;
};

struct Inst : BSP
{
    // in BSP_Leaf::inst / BSP_NodeShare::inst
	Inst* next;
	Inst* prev;    

    Mesh* mesh;
    double tm[16]; // absoulte! mesh->world

    Inst* share_next; // next instance sharing same mesh

    int /*FLAGS*/ flags; 
    char* name;

	void UpdateBox()
	{
		float w[4];
		Vert* v = mesh->head_vert;

		if (v)
		{
			Product(tm, v->xyzw, w);
			bbox[0] = w[0];
			bbox[1] = w[0];
			bbox[2] = w[1];
			bbox[3] = w[1];
			bbox[4] = w[2];
			bbox[5] = w[2];
			v = v->next;
		}

		while (v)
		{
			Product(tm, v->xyzw, w);
			bbox[0] = fminf(bbox[0], w[0]);
			bbox[1] = fmaxf(bbox[1], w[0]);
			bbox[2] = fminf(bbox[2], w[1]);
			bbox[3] = fmaxf(bbox[3], w[1]);
			bbox[4] = fminf(bbox[4], w[2]);
			bbox[5] = fmaxf(bbox[5], w[2]);
			v = v->next;
		}
	}

    bool HitFace(double ray[6], double ret[3], double nrm[3])
    {
        if (!mesh)
            return false;

        bool flag = false;

        Face* f = mesh->head_face; 
        while (f)
        {
            double v0[4], v1[4], v2[4];
            Product(tm,f->abc[0]->xyzw,v0);
            Product(tm,f->abc[1]->xyzw,v1);
            Product(tm,f->abc[2]->xyzw,v2);

            double hit[3];
            if (RayIntersectsTriangle(ray,v0,v1,v2,hit))
            {
                if (hit[2] > ret[2])
                {
					ret[0] = hit[0];
					ret[1] = hit[1];
					ret[2] = hit[2];

                    // TODO: if (nrm) calc normal -> nrm
					// ...

                    flag = true;
                }
            }

            f=f->next;
        }

        return flag;
    }
};

int bsp_tests=0;
int bsp_insts=0;
int bsp_nodes=0;

struct World
{
    int meshes;
    Mesh* head_mesh;
    Mesh* tail_mesh;

    Mesh* LoadMesh(const char* path, const char* name);

    Mesh* AddMesh(const char* name = 0, void* cookie = 0)
    {
        Mesh* m = (Mesh*)malloc(sizeof(Mesh));

        m->world = this;
        m->type = Mesh::MESH_TYPE_3D;
        m->name = name ? strdup(name) : 0;
        m->cookie = cookie;

        m->next = 0;
        m->prev = tail_mesh;
        if (tail_mesh)
            tail_mesh->next = m;
        else
            head_mesh = m;
        tail_mesh = m;    

        m->share_list = 0;
 
        m->verts = 0;
        m->head_vert = 0;
        m->tail_vert = 0;
        
        m->faces = 0;
        m->head_face = 0;
        m->tail_face = 0;

        m->lines = 0;
        m->head_line = 0;
        m->tail_line = 0;

        memset(m->bbox,0,sizeof(float[6]));

        meshes++;

        return m;
    }

    bool DelMesh(Mesh* m)
    {
        if (!m || m->world != this)
            return false;

        // kill sharing insts
        Inst* i = m->share_list;
        while (m->share_list)
            DelInst(m->share_list);

        Face* f = m->head_face;
        while (f)
        {
            Face* n = f->next;
            free(f);
            f=n;
        }

        Line* l = m->head_line;
        while (l)
        {
            Line* n = l->next;
            free(l);
            l=n;
        }        

        Vert* v = m->head_vert;
        while (v)
        {
            Vert* n = v->next;
            free(v);
            v=n;
        }

        if (m->name)
            free(m->name);

        if (m->prev)
            m->prev->next = m->next;
        else
            head_mesh = m->next;

        if (m->next)
            m->next->prev = m->prev;
        else
            tail_mesh = m->prev;

        free(m);
        meshes--;

        return true;
    }

    // all insts
    int insts;

    // only non-bsp
    Inst* head_inst; 
    Inst* tail_inst;

    Inst* AddInst(Mesh* m, int flags, const double tm[16], const char* name)
    {
        if (!m || m->world != this)
            return 0;

        Inst* i = (Inst*)malloc(sizeof(Inst));

		if (tm)
		{
			memcpy(i->tm, tm, sizeof(double[16]));
		}
		else
		{
			memset(i->tm, 0, sizeof(double[16]));
			i->tm[0] = i->tm[5] = i->tm[10] = i->tm[15] = 1.0;
			i->bbox[0] = m->bbox[0];
			i->bbox[1] = m->bbox[1];
			i->bbox[2] = m->bbox[2];
			i->bbox[3] = m->bbox[3];
			i->bbox[4] = m->bbox[4];
			i->bbox[5] = m->bbox[5];
		}

        i->name = name ? strdup(name) : 0;

        i->mesh = m;

		i->UpdateBox();

        i->type = BSP::BSP_TYPE_INST;
        i->flags = flags;
        i->bsp_parent = 0;

        if (m)
        {
            i->share_next = m->share_list;
            m->share_list = i;
        }
        else
            i->share_next = 0;

        i->next = 0;
        i->prev = tail_inst;
        if (tail_inst)
            tail_inst->next = i;
        else
            head_inst = i;
        tail_inst = i;    
        
        insts++;
        return i;
    }

    bool DelInst(Inst* i)
    {
        if (!i || !i->mesh || i->mesh->world != this)
            return false;

        if (i->mesh)
        {
            Inst** s = &i->mesh->share_list;
            while (*s != i)
                s = &(*s)->share_next;
            *s = (*s)->share_next;
        }

        if (!i->bsp_parent)
        {
            if (i->prev)
                i->prev->next = i->next;
            else
                head_inst = i->next;

            if (i->next)
                i->next->prev = i->prev;
            else
                tail_inst = i->prev;
        }
        else
        {
            if (i->bsp_parent->type == BSP::BSP_TYPE_LEAF)
            {
                BSP_Leaf* leaf = (BSP_Leaf*)i->bsp_parent;

                if (i->prev)
                    i->prev->next = i->next;
                else
                    leaf->head = i->next;

                if (i->next)
                    i->next->prev = i->prev;
                else
                    leaf->tail = i->prev;

                if (leaf->head == 0)
                {
                    // do ancestors cleanup
                    // ...
                }
            }
            else
            if (i->bsp_parent->type == BSP::BSP_TYPE_NODE_SHARE)
            {
                BSP_NodeShare* share = (BSP_NodeShare*)i->bsp_parent;

                if (share->bsp_child[0] == i)
                    share->bsp_child[0] = 0;
                else
                if (share->bsp_child[1] == i)
                    share->bsp_child[1] = 0;
                else
                {
                    if (i->prev)
                        i->prev->next = i->next;
                    else
                        share->head = i->next;

                    if (i->next)
                        i->next->prev = i->prev;
                    else
                        share->tail = i->prev;
                }

                if (share->head == 0 && share->bsp_child[0]==0 && share->bsp_child[1]==0)
                {
                    // do ancestors cleanup
                    // ...
                }
            }
            else
            if (i->bsp_parent->type == BSP::BSP_TYPE_NODE)
            {
                BSP_Node* node = (BSP_Node*)i->bsp_parent;
                if (node->bsp_child[0] == i)
                    node->bsp_child[0] = 0;
                else
                if (node->bsp_child[1] == i)
                    node->bsp_child[1] = 0;

                if (node->bsp_child[0]==0 && node->bsp_child[1]==0)
                {
                    // do ancestors cleanup
                    // ...
                }
            }
            else
            {
                assert(0);
            }
        }

        if (i->name)
            free(i->name);

        if (editable == i)
            editable = 0;

        if (root == i)
            root = 0;
           
        insts--;
        free(i);

        return true;
    }

    // currently selected instance (its mesh) for editting
    // overrides visibility?
    Inst* editable;

    // now we want to form a tree of Insts
    BSP* root;

    void DeleteBSP(BSP* bsp)
    {
        if (bsp->type == BSP::BSP_TYPE_NODE)
        {
            BSP_Node* node = (BSP_Node*)bsp;

            if (node->bsp_child[0])
                DeleteBSP(node->bsp_child[0]);
            if (node->bsp_child[1])
                DeleteBSP(node->bsp_child[1]);
            free (bsp);
        }
        else
        if (bsp->type == BSP::BSP_TYPE_NODE_SHARE)
        {
            BSP_NodeShare* share = (BSP_NodeShare*)bsp;

            if (share->bsp_child[0])
                DeleteBSP(share->bsp_child[0]);
            if (share->bsp_child[1])
                DeleteBSP(share->bsp_child[1]);

            if (share->head)
            {
                Inst* i = share->head;
                while (i)
                {
                    i->bsp_parent = 0;
                    i=i->next;
                }

                if (tail_inst)
                {
                    share->head->prev = tail_inst;
                    tail_inst->next = share->head;
                    tail_inst = share->tail;
                }
                else
                {
                    head_inst = share->head; 
                    tail_inst = share->tail;
                }
            }
            free (bsp);
        }        
        else
        if (bsp->type == BSP::BSP_TYPE_LEAF)
        {
            BSP_Leaf* leaf = (BSP_Leaf*)bsp;
            if (leaf->head)
            {
                Inst* i = leaf->head;
                while (i)
                {
                    i->bsp_parent = 0;
                    i=i->next;
                }

                if (tail_inst)
                {
                    leaf->head->prev = tail_inst;
                    tail_inst->next = leaf->head;
                    tail_inst = leaf->tail;
                }
                else
                {
                    head_inst = leaf->head; 
                    tail_inst = leaf->tail;
                }
            }
            free (bsp);
        }        
        else
        if (bsp->type == BSP::BSP_TYPE_INST)
        {
            Inst* inst = (Inst*)bsp;

            bsp->bsp_parent = 0;
            inst->next=0;
            inst->prev = tail_inst;
            if (tail_inst)
                tail_inst->next = inst;
            else
                head_inst = inst;
            tail_inst=inst;
        }
        else
        {
            assert(0);
        }
        
    }

    struct BSP_Item
    {
        Inst* inst;
        float area;
    };

    BSP* SplitBSP(BSP_Item* arr, int num)
    {
        assert(num>0);
        
        if (num == 1)
        {
            Inst* inst = arr[0].inst;
            inst->bsp_parent = 0;
            inst->prev = 0;
            inst->next = 0;
            return inst;
        }

        struct CentroidSorter
        {
            static int sortX(const void* a, const void* b)
            {
                Inst* ia = ((BSP_Item*)a)->inst;
                Inst* ib = ((BSP_Item*)b)->inst;

                float diff = (ia->bbox[0]+ia->bbox[1])-(ib->bbox[0]+ib->bbox[1]);
                if (diff<0)
                    return -1;
                if (diff>0)
                    return +1;
                return 0;
            }
            static int sortY(const void* a, const void* b)
            {
                Inst* ia = ((BSP_Item*)a)->inst;
                Inst* ib = ((BSP_Item*)b)->inst;

                float diff = (ia->bbox[2]+ia->bbox[3])-(ib->bbox[2]+ib->bbox[3]);
                if (diff<0)
                    return -1;
                if (diff>0)
                    return +1;
                return 0;
            }
            static int sortZ(const void* a, const void* b)
            {
                Inst* ia = ((BSP_Item*)a)->inst;
                Inst* ib = ((BSP_Item*)b)->inst;

                float diff = (ia->bbox[4]+ia->bbox[5])-(ib->bbox[4]+ib->bbox[5]);
                if (diff<0)
                    return -1;
                if (diff>0)
                    return +1;
                return 0;
            }
        };

        static int (*sort[3])(const void* a, const void* b) = 
        {
            CentroidSorter::sortX,
            CentroidSorter::sortY,
            CentroidSorter::sortZ
        };

        float best_cost = -1;
        int best_axis = -1;
        int best_item = -1;

        // actualy it could be better to splin only in x and y (axis<2)
        for (int axis=0; axis<3; axis++)
        {
            qsort(arr, num, sizeof(BSP_Item), sort[axis]);

            float lo_bbox[6] =
            {
                arr[0].inst->bbox[0],
                arr[0].inst->bbox[1],
                arr[0].inst->bbox[2],
                arr[0].inst->bbox[3],
                arr[0].inst->bbox[4],
                arr[0].inst->bbox[5]
            };

            for (int i=0; i<num; i++)
            {
                lo_bbox[0] = fminf( lo_bbox[0], arr[i].inst->bbox[0]);
                lo_bbox[1] = fmaxf( lo_bbox[1], arr[i].inst->bbox[1]);
                lo_bbox[2] = fminf( lo_bbox[2], arr[i].inst->bbox[2]);
                lo_bbox[3] = fmaxf( lo_bbox[3], arr[i].inst->bbox[3]);
                lo_bbox[4] = fminf( lo_bbox[4], arr[i].inst->bbox[4]);
                lo_bbox[5] = fmaxf( lo_bbox[5], arr[i].inst->bbox[5]);

                arr[i].area = 
                    (lo_bbox[1]-lo_bbox[0]) * (lo_bbox[3]-lo_bbox[2]) * HEIGHT_SCALE +
                    (lo_bbox[3]-lo_bbox[2]) * (lo_bbox[5]-lo_bbox[4]) +
                    (lo_bbox[5]-lo_bbox[4]) * (lo_bbox[1]-lo_bbox[0]);                
            }

            float hi_bbox[6] =
            {
                arr[num-1].inst->bbox[0],
                arr[num-1].inst->bbox[1],
                arr[num-1].inst->bbox[2],
                arr[num-1].inst->bbox[3],
                arr[num-1].inst->bbox[4],
                arr[num-1].inst->bbox[5]
            };

            for (int i=num-1; i>0; i--)
            {
                hi_bbox[0] = fminf( hi_bbox[0], arr[i].inst->bbox[0]);
                hi_bbox[1] = fmaxf( hi_bbox[1], arr[i].inst->bbox[1]);
                hi_bbox[2] = fminf( hi_bbox[2], arr[i].inst->bbox[2]);
                hi_bbox[3] = fmaxf( hi_bbox[3], arr[i].inst->bbox[3]);
                hi_bbox[4] = fminf( hi_bbox[4], arr[i].inst->bbox[4]);
                hi_bbox[5] = fmaxf( hi_bbox[5], arr[i].inst->bbox[5]);

                float area = 
                    (hi_bbox[1]-hi_bbox[0]) * (hi_bbox[3]-hi_bbox[2]) * HEIGHT_SCALE +
                    (hi_bbox[3]-hi_bbox[2]) * (hi_bbox[5]-hi_bbox[4]) +
                    (hi_bbox[5]-hi_bbox[4]) * (hi_bbox[1]-hi_bbox[0]);

                // cost of {0..i-1}, {i..num-1}
                float cost = arr[i-1].area * i + area * (num-i);
                if (cost < best_cost || best_cost < 0)
                {
                    best_cost = cost;
                    best_item = i;
                    best_axis = axis;
                }
            }
        }

        if (best_axis != -1)
        {
            if (best_cost + arr[num-1].area * 2 > arr[num-1].area * num)
                best_axis = -1;
        }

        if (best_axis == -1)
        {
            // fill final list of instances

            BSP_Leaf* leaf = (BSP_Leaf*)malloc(sizeof(BSP_Leaf));
            leaf->bsp_parent = 0;
            leaf->type = BSP::BSP_TYPE_LEAF;

            leaf->bbox[0] = arr[0].inst->bbox[0];
            leaf->bbox[1] = arr[0].inst->bbox[1];
            leaf->bbox[2] = arr[0].inst->bbox[2];
            leaf->bbox[3] = arr[0].inst->bbox[3];
            leaf->bbox[4] = arr[0].inst->bbox[4];
            leaf->bbox[5] = arr[0].inst->bbox[5];
            
            leaf->head = arr[0].inst;
            leaf->tail = arr[num-1].inst;
            
            arr[0].inst->prev = 0;
            arr[num-1].inst->next = 0;

            arr[0].inst->bsp_parent = leaf;
            if (num>1)
            {
                arr[0].inst->next = arr[1].inst;
                arr[num-1].inst->prev = arr[num-2].inst;
                arr[num-1].inst->bsp_parent = leaf;

                leaf->bbox[0] = fminf( leaf->bbox[0], arr[num-1].inst->bbox[0]);
                leaf->bbox[1] = fmaxf( leaf->bbox[1], arr[num-1].inst->bbox[1]);
                leaf->bbox[2] = fminf( leaf->bbox[2], arr[num-1].inst->bbox[2]);
                leaf->bbox[3] = fmaxf( leaf->bbox[3], arr[num-1].inst->bbox[3]);
                leaf->bbox[4] = fminf( leaf->bbox[4], arr[num-1].inst->bbox[4]);
                leaf->bbox[5] = fmaxf( leaf->bbox[5], arr[num-1].inst->bbox[5]);                
            }

            for (int i=1; i<num-1; i++)
            {
                leaf->bbox[0] = fminf( leaf->bbox[0], arr[i].inst->bbox[0]);
                leaf->bbox[1] = fmaxf( leaf->bbox[1], arr[i].inst->bbox[1]);
                leaf->bbox[2] = fminf( leaf->bbox[2], arr[i].inst->bbox[2]);
                leaf->bbox[3] = fmaxf( leaf->bbox[3], arr[i].inst->bbox[3]);
                leaf->bbox[4] = fminf( leaf->bbox[4], arr[i].inst->bbox[4]);
                leaf->bbox[5] = fmaxf( leaf->bbox[5], arr[i].inst->bbox[5]);  
                                
                arr[i].inst->bsp_parent = leaf;
                arr[i].inst->prev = arr[i-1].inst;
                arr[i].inst->next = arr[i+1].inst;
            }

            assert(leaf->head);
            assert(leaf->tail);
            assert(num==1 && leaf->head == leaf->tail || num!=1 && leaf->head != leaf->tail);
            assert(leaf->head->prev == 0);
            assert(leaf->tail->next == 0);

            Inst* i = leaf->head;
            int c = 0;
            while (i)
            {
                c++;
                i=i->next;
                if (i)
                    assert(i->prev->next == i);
            }

            assert(c==num);

            i = leaf->tail;
            c = 0;
            while (i)
            {
                c++;
                i=i->prev;
                if (i)
                    assert(i->next->prev == i);
            }

            assert(c==num);

            return leaf;
        }
        else
        {
            int axis = best_axis;
            qsort(arr, num, sizeof(BSP_Item), sort[axis]);

            BSP_Node* node = (BSP_Node*)malloc(sizeof(BSP_Node));
            node->bsp_parent = 0;
            node->type = BSP::BSP_TYPE_NODE;

            node->bsp_child[0] = SplitBSP(arr + 0, best_item);
            node->bsp_child[0]->bsp_parent = node;

            node->bsp_child[1] = SplitBSP(arr + best_item, num - best_item);
            node->bsp_child[1]->bsp_parent = node;

            node->bbox[0] = fminf( node->bsp_child[0]->bbox[0], node->bsp_child[1]->bbox[0]);
            node->bbox[1] = fmaxf( node->bsp_child[0]->bbox[1], node->bsp_child[1]->bbox[1]);
            node->bbox[2] = fminf( node->bsp_child[0]->bbox[2], node->bsp_child[1]->bbox[2]);
            node->bbox[3] = fmaxf( node->bsp_child[0]->bbox[3], node->bsp_child[1]->bbox[3]);
            node->bbox[4] = fminf( node->bsp_child[0]->bbox[4], node->bsp_child[1]->bbox[4]);
            node->bbox[5] = fmaxf( node->bsp_child[0]->bbox[5], node->bsp_child[1]->bbox[5]);               

            return node;
        }
    }

	void Rebuild(bool boxes)
	{
		if (root)
		{
			DeleteBSP(root);
			root = 0;
		}

        if (!insts)
            return;

        // MAY BE SLOW: 1/2 * num^3
        /*
        int num = 0;

        BSP** arr = (BSP**)malloc(sizeof(BSP*) * insts);
        
        for (Inst* inst = head_inst; inst; inst=inst->next)
        {
            if (inst->flags & INST_USE_TREE)
                arr[num++] = inst;
        }

        while (num>1)
        {
            int a = 0;
            int b = 1;
            float e = -1;

            for (int u=0; u<num-1; u++)
            {
                for (int v=u+1; v<num; v++)
                {
                    float bbox[6] =
                    {
                        fminf( arr[u]->bbox[0] , arr[v]->bbox[0] ),
                        fmaxf( arr[u]->bbox[1] , arr[v]->bbox[1] ),
                        fminf( arr[u]->bbox[2] , arr[v]->bbox[2] ),
                        fmaxf( arr[u]->bbox[3] , arr[v]->bbox[3] ),
                        fminf( arr[u]->bbox[4] , arr[v]->bbox[4] ),
                        fmaxf( arr[u]->bbox[5] , arr[v]->bbox[5] )
                    };

                    float vol = (bbox[1]-bbox[0]) * (bbox[3]-bbox[2]) * (bbox[5]-bbox[4]);
                    
                    float u_vol = (arr[u]->bbox[1]-arr[u]->bbox[0]) * (arr[u]->bbox[3]-arr[u]->bbox[2]) * (arr[u]->bbox[5]-arr[u]->bbox[4]);
                    float v_vol = (arr[v]->bbox[1]-arr[v]->bbox[0]) * (arr[v]->bbox[3]-arr[v]->bbox[2]) * (arr[v]->bbox[5]-arr[v]->bbox[4]);
                    
                    vol -= u_vol + v_vol; // minimize volumne expansion

                    // minimize volume difference between children
                    if (u_vol > v_vol)
                        vol += u_vol-v_vol;
                    else
                        vol += v_vol-u_vol;

                    if (vol < e || e<0)
                    {
                        a = u;
                        b = v;
                        e = vol;
                    }
                }
            }

            BSP_Node* node = (BSP_Node*)malloc(sizeof(BSP_Node));

            node->bsp_parent = 0;
            node->type = BSP::BSP_TYPE_NODE;

            node->bsp_child[0] = arr[a];
            node->bsp_child[1] = arr[b];

            node->bbox[0] = fminf( arr[a]->bbox[0] , arr[b]->bbox[0] );
            node->bbox[1] = fmaxf( arr[a]->bbox[1] , arr[b]->bbox[1] );
            node->bbox[2] = fminf( arr[a]->bbox[2] , arr[b]->bbox[2] );
            node->bbox[3] = fmaxf( arr[a]->bbox[3] , arr[b]->bbox[3] );
            node->bbox[4] = fminf( arr[a]->bbox[4] , arr[b]->bbox[4] );
            node->bbox[5] = fmaxf( arr[a]->bbox[5] , arr[b]->bbox[5] );

            num--;
            if (b!=num)
                arr[b] = arr[num];

            arr[a] = node;
        }
        root = arr[0];
        free(arr);
        */

        // LET'S TRY:
        // https://graphics.stanford.edu/~boulos/papers/togbvh.pdf
        // http://www.cs.uu.nl/docs/vakken/magr/2016-2017/slides/lecture%2003%20-%20the%20perfect%20BVH.pdf

        // KEY IDEAS
        // leaf nodes need ability of multiple objects encapsulation
        // object can be referenced by both leaf siblings -> use NodeShare in place of Node !!! 
        // ... need only to ensure that union of both leaf bboxes fully encapsulates that object

        BSP_Item* arr = (BSP_Item*)malloc(sizeof(BSP_Item) * insts);

        int count = 0;
        for (Inst* inst = head_inst; inst; )
        {
			// update inst bbox
			if (boxes)
			{
				inst->UpdateBox();
			}

            Inst* next = inst->next;
            if (inst->flags & INST_USE_TREE)
            {
                arr[count++].inst = inst;

                // extract!
                if (inst->prev)
                    inst->prev->next = inst->next;
                else
                    head_inst = inst->next;
                if (inst->next)
                    inst->next->prev = inst->prev;
                else
                    tail_inst = inst->prev;
            }
            inst = next;
        }

        if (count)
        {
            // split recursively!
            root = SplitBSP(arr, count);

            if (!root)
            {
                // if failed we need to put them back
                for (int i=0; i<count; i++)
                {
                    Inst* inst = arr[i].inst;
                    inst->bsp_parent = 0;
                    inst->prev = tail_inst;
                    inst->next = 0;
                    if (tail_inst)
                        tail_inst->next = inst;
                    else
                        head_inst = inst;
                    tail_inst = inst;                        
                }
            }

            free(arr);
        }
    }

	static Inst* HitWorld0(BSP* q, double ray[6], double ret[3], double nrm[3])
	{
        const float x[2] = {q->bbox[0],q->bbox[1]};
		const float y[2] = {q->bbox[2],q->bbox[3]};
		const float z[2] = {q->bbox[4],q->bbox[5]};

		if (ray[1] - z[0] * ray[3] + ray[5] * x[1] > 0 ||
			ray[5] * y[1] - ray[0] - z[0] * ray[4] > 0 ||
			ray[2] - ray[4] * x[0] + ray[3] * y[1] > 0 ||
			z[1] * ray[3] - ray[5] * x[0] - ray[1] > 0 ||
			ray[0] + z[1] * ray[4] - ray[5] * y[0] > 0 ||
			ray[4] * x[1] - ray[3] * y[0] - ray[2] > 0)
			return 0;

		if (q->type == BSP::TYPE::BSP_TYPE_INST)
		{
			Inst* inst = (Inst*)q;
			if (inst->HitFace(ray, ret, nrm))
				return inst;
			else
				return 0;
		}
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE)
        {
            BSP_Node* n = (BSP_Node*)q;
            Inst* i = HitWorld0(n->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld0(n->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE_SHARE)
        {
            BSP_NodeShare* s = (BSP_NodeShare*)q;

            Inst* i = HitWorld0(s->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld0(s->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;

            j = s->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_LEAF)
        {
            BSP_Leaf* l = (BSP_Leaf*)q;

            Inst* i = 0;
            Inst* j = l->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }
        
		return 0;
	}

	static Inst* HitWorld1(BSP* q, double ray[6], double ret[3], double nrm[3])
	{
		const float x[2] = { q->bbox[0],q->bbox[1] };
		const float y[2] = { q->bbox[2],q->bbox[3] };
		const float z[2] = { q->bbox[4],q->bbox[5] };

		if (ray[5] * y[1] - ray[0] - z[0] * ray[4] > 0 ||
			z[0] * ray[3] - ray[5] * x[0] - ray[1] > 0 ||
			ray[2] - ray[4] * x[0] + ray[3] * y[0] > 0 ||
			ray[0] + z[1] * ray[4] - ray[5] * y[0] > 0 ||
			ray[1] - z[0] * ray[3] + ray[5] * x[1] > 0 ||
			ray[4] * x[1] - ray[3] * y[1] - ray[2] > 0)
			return 0;

		if (q->type == BSP::TYPE::BSP_TYPE_INST)
		{
			Inst* inst = (Inst*)q;
			if (inst->HitFace(ray, ret, nrm))
				return inst;
			else
				return 0;
		}
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE)
        {
            BSP_Node* n = (BSP_Node*)q;
            Inst* i = HitWorld1(n->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld1(n->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE_SHARE)
        {
            BSP_NodeShare* s = (BSP_NodeShare*)q;

            Inst* i = HitWorld1(s->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld1(s->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;

            j = s->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_LEAF)
        {
            BSP_Leaf* l = (BSP_Leaf*)q;

            Inst* i = 0;
            Inst* j = l->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }

		return 0;
	}

	static Inst* HitWorld2(BSP* q, double ray[6], double ret[3], double nrm[3])
	{
		const float x[2] = { q->bbox[0],q->bbox[1] };
		const float y[2] = { q->bbox[2],q->bbox[3] };
		const float z[2] = { q->bbox[4],q->bbox[5] };

		if (ray[0] + z[0] * ray[4] - ray[5] * y[0] > 0 ||
			ray[1] - z[0] * ray[3] + ray[5] * x[1] > 0 ||
			ray[2] + ray[3] * y[1] - ray[4] * x[1] > 0 ||
			ray[5] * y[1] - ray[0] - z[1] * ray[4] > 0 ||
			z[1] * ray[3] - ray[5] * x[0] - ray[1] > 0 ||
			ray[4] * x[0] - ray[3] * y[0] - ray[2] > 0)
			return 0;

		if (q->type == BSP::TYPE::BSP_TYPE_INST)
		{
			Inst* inst = (Inst*)q;
			if (inst->HitFace(ray, ret, nrm))
				return inst;
			else
				return 0;
		}
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE)
        {
            BSP_Node* n = (BSP_Node*)q;
            Inst* i = HitWorld2(n->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld2(n->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE_SHARE)
        {
            BSP_NodeShare* s = (BSP_NodeShare*)q;

            Inst* i = HitWorld2(s->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld2(s->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;

            j = s->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_LEAF)
        {
            BSP_Leaf* l = (BSP_Leaf*)q;

            Inst* i = 0;
            Inst* j = l->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }

		return 0;
	}

	static Inst* HitWorld3(BSP* q, double ray[6], double ret[3], double nrm[3])
	{
		const float x[2] = { q->bbox[0],q->bbox[1] };
		const float y[2] = { q->bbox[2],q->bbox[3] };
		const float z[2] = { q->bbox[4],q->bbox[5] };

		if (z[0] * ray[3] - ray[5] * x[0] - ray[1] > 0 ||
			ray[0] + z[0] * ray[4] - ray[5] * y[0] > 0 ||
			ray[2] - ray[4] * x[1] + ray[3] * y[0] > 0 ||
			ray[1] - z[1] * ray[3] + ray[5] * x[1] > 0 ||
			ray[5] * y[1] - ray[0] - z[1] * ray[4] > 0 ||
			ray[4] * x[0] - ray[3] * y[1] - ray[2] > 0)
			return 0;

		if (q->type == BSP::TYPE::BSP_TYPE_INST)
		{
			Inst* inst = (Inst*)q;
			if (inst->HitFace(ray, ret, nrm))
				return inst;
			else
				return 0;
		}
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE)
        {
            BSP_Node* n = (BSP_Node*)q;
            Inst* i = HitWorld3(n->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld3(n->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_NODE_SHARE)
        {
            BSP_NodeShare* s = (BSP_NodeShare*)q;

            Inst* i = HitWorld3(s->bsp_child[0], ray, ret, nrm);
            Inst* j = HitWorld3(s->bsp_child[1], ray, ret, nrm);
            i = j ? j : i;

            j = s->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }
        else
        if (q->type == BSP::TYPE::BSP_TYPE_LEAF)
        {
            BSP_Leaf* l = (BSP_Leaf*)q;

            Inst* i = 0;
            Inst* j = l->head;
            while (j)
            {
                if (j->HitFace(ray, ret, nrm))
                    i=j;
                j=j->next;
            }
            return i;
        }

		return 0;
	}

    // RAY HIT using plucker
    Inst* HitWorld(double p[3], double v[3], double ret[3], double nrm[3])
    {
		// p should be projected to the BOTTOM plane!
		double ray[] =
		{
			p[1] * v[2] - p[2] * v[1],
			p[2] * v[0] - p[0] * v[2],
			p[0] * v[1] - p[1] * v[0],
			v[0], v[1], v[2],
			p[0], p[1], p[2] // used by triangle-ray intersection
		};

		int sign_case = 0;

		if (v[0] >= 0)
			sign_case |= 1;
		if (v[1] >= 0)
			sign_case |= 2;
		if (v[2] >= 0)
			sign_case |= 4;

		assert((sign_case & 4) == 0); // watching from the bottom? -> raytraced reflections?

		static Inst* (*const func_vect[])(BSP* q, double ray[6], double ret[3], double nrm[3]) =
		{
			HitWorld0,
			HitWorld1,
			HitWorld2,
			HitWorld3
		};

		ret[0] = p[0];
		ret[1] = p[1];
		ret[2] = p[2];

		Inst* inst = func_vect[sign_case](root, ray, ret, nrm);
		return inst;
    }

    static void QueryBSP(int level, BSP* bsp, int planes, double plane[][4], void (*cb)(int level, const float bbox[6], void* cookie), void* cookie)
    {
        // temporarily don't check planes
        cb(level, bsp->bbox,cookie);

        if (bsp->type == BSP::BSP_TYPE_NODE)
        {
            BSP_Node* n = (BSP_Node*)bsp;

            if (n->bsp_child[0])
                QueryBSP(level+1, n->bsp_child[0], planes, plane, cb, cookie);
            if (n->bsp_child[1])
                QueryBSP(level+1, n->bsp_child[1], planes, plane, cb, cookie);
        }
        else
        if (bsp->type == BSP::BSP_TYPE_NODE_SHARE)
        {
            BSP_NodeShare* s = (BSP_NodeShare*)bsp;

            if (s->bsp_child[0])
                QueryBSP(level+1, s->bsp_child[0], planes, plane, cb, cookie);
            if (s->bsp_child[1])
                QueryBSP(level+1, s->bsp_child[1], planes, plane, cb, cookie);

            Inst* i = s->head;
            while (i)
            {
                QueryBSP(level+1, i, planes, plane, cb, cookie);
                i=i->next;
            }
        }
        else
        if (bsp->type == BSP::BSP_TYPE_LEAF)
        {
            BSP_Leaf* l = (BSP_Leaf*)bsp;

            Inst* i = l->head;
            while (i)
            {
                QueryBSP(level+1, i, planes, plane, cb, cookie);
                i=i->next;
            }
        }
        else
        {
            assert(bsp->type == BSP::BSP_TYPE_INST);
        }
        
    }

    // MESHES IN HULL

    // recursive no clipping
    static void Query(BSP* bsp, void (*cb)(Mesh* m, const double tm[16], void* cookie), void* cookie)
    {
        if (bsp->type == BSP::BSP_TYPE_LEAF)
        {
            bsp_nodes++;
            Inst* i = ((BSP_Leaf*)bsp)->head;
            while (i)
            {
                Query(i,cb,cookie);
                i=i->next;
            }
        }
        else
        if (bsp->type == BSP::BSP_TYPE_INST)
        {
            bsp_insts++;
            Inst* i = (Inst*)bsp;
            cb(i->mesh, i->tm, cookie);
        }
        else
        if (bsp->type == BSP::BSP_TYPE_NODE)
        {
            bsp_nodes++;
            BSP_Node* n = (BSP_Node*)bsp;
            if (n->bsp_child[0])
                Query(n->bsp_child[0],cb,cookie);
            if (n->bsp_child[1])
                Query(n->bsp_child[1],cb,cookie);
        }
        else
        if (bsp->type == BSP::BSP_TYPE_NODE_SHARE)
        {
            bsp_nodes++;
            BSP_NodeShare* s = (BSP_NodeShare*)bsp;
            if (s->bsp_child[0])
                Query(s->bsp_child[0],cb,cookie);
            if (s->bsp_child[1])
                Query(s->bsp_child[1],cb,cookie);
            Inst* i = s->head;
            while (i)
            {
                Query(i,cb,cookie);
                i=i->next;
            }                
        }
        else
        {
            assert(0);
        }
        
    }

    // recursive
    static void Query(BSP* bsp, int planes, double* plane[], void (*cb)(Mesh* m, const double tm[16], void* cookie), void* cookie)
    {
        float c[4] = { bsp->bbox[0], bsp->bbox[2], bsp->bbox[4], 1 }; // 0,0,0

        bsp_tests++;

        for (int i = 0; i < planes; i++)
        {
            int neg_pos[2] = { 0,0 };

            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[0] = bsp->bbox[1]; // 1,0,0
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[1] = bsp->bbox[3]; // 1,1,0
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[0] = bsp->bbox[0]; // 0,1,0
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[2] = bsp->bbox[5]; // 0,1,1
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[0] = bsp->bbox[1]; // 1,1,1
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[1] = bsp->bbox[2]; // 1,0,1
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[0] = bsp->bbox[0]; // 0,0,1
            neg_pos[PositiveProduct(plane[i], c)] ++;

            c[2] = bsp->bbox[4]; // 0,0,0

            if (neg_pos[0] == 8)
                return;

            if (neg_pos[1] == 8)
            {
                planes--;
                if (i < planes)
                {
                    double* swap = plane[i];
                    plane[i] = plane[planes];
                    plane[planes] = swap;
                }
                i--;
            }
        }

        if (bsp->type == BSP::BSP_TYPE_INST)
        {
            bsp_insts++;
            Inst* i = (Inst*)bsp;
            cb(i->mesh, i->tm, cookie);
        }
        else
        if (bsp->type == BSP::BSP_TYPE_NODE)        
        {
            bsp_nodes++;
            BSP_Node* n = (BSP_Node*)bsp;
            if (planes)
            {
                if (n->bsp_child[0])
                    Query(n->bsp_child[0],planes,plane,cb,cookie);
                if (n->bsp_child[1])
                    Query(n->bsp_child[1],planes,plane,cb,cookie);
            }
            else
            {
                if (n->bsp_child[0])
                    Query(n->bsp_child[0],cb,cookie);
                if (n->bsp_child[1])
                    Query(n->bsp_child[1],cb,cookie);
            }
        }
        else
        if (bsp->type == BSP::BSP_TYPE_NODE_SHARE)
        {
            bsp_nodes++;
            BSP_NodeShare* s = (BSP_NodeShare*)bsp;
            if (planes)
            {
                if (s->bsp_child[0])
                    Query(s->bsp_child[0],planes,plane,cb,cookie);
                if (s->bsp_child[1])
                    Query(s->bsp_child[1],planes,plane,cb,cookie);

                Inst* i = s->head;
                while (i)
                {
                    Query(i,planes,plane,cb,cookie);
                    i=i->next;
                }                
            }
            else
            {
                if (s->bsp_child[0])
                    Query(s->bsp_child[0],cb,cookie);
                if (s->bsp_child[1])
                    Query(s->bsp_child[1],cb,cookie);

                Inst* i = s->head;
                while (i)
                {
                    Query(i,cb,cookie);
                    i=i->next;
                }                
            }
        }        
        else
        if (bsp->type == BSP::BSP_TYPE_LEAF)
        {
            bsp_nodes++;
            BSP_Leaf* l = (BSP_Leaf*)bsp;
            if (planes)
            {
                Inst* i = l->head;
                while (i)
                {
                    Query(i,planes,plane,cb,cookie);
                    i=i->next;
                }                
            }
            else
            {
                Inst* i = l->head;
                while (i)
                {
                    Query(i,cb,cookie);
                    i=i->next;
                }                
            }
        }        
        else
        {
            assert(0);
        }
    }

    // main
    void Query(int planes, double plane[][4], void (*cb)(Mesh* m, const double tm[16], void* cookie), void* cookie)
    {
        bsp_tests=0;
        bsp_insts=0;
        bsp_nodes=0;

        // static first
        if (root)
        {
			if (planes > 0)
			{
				double* pp[4] = { plane[0],plane[1],plane[2],plane[3] };
				Query(root, planes, pp, cb, cookie);
			}
			else
			{
				Query(root, cb, cookie);
			}
        }

		// dynamic after
		Inst* i = head_inst;
		if (planes > 0)
		{
			double* pp[4] = { plane[0],plane[1],plane[2],plane[3] };
			while (i)
			{
				Query(i, planes, pp, cb, cookie);
				i = i->next;
			}
		}
		else
		{
			while (i)
			{
				Query(i, cb, cookie);
				i = i->next;
			}
		}
	}
};


bool Mesh::Update(const char* path)
{
    FILE* f = fopen(path,"rt");
	if (!f)
		return false;

	int plannar = 0x7 | 0x8;

	char buf[1024];
	char tail_str[1024];

	int num_verts = -1;
	int num_faces = -1;
	int element = 0;

	bool face_props = false;
	int vert_props = 0;

	// file header
	while (fgets(buf, 1024, f))
	{
		int len = (int)strlen(buf);
		while (len && (buf[len - 1] == ' ' || buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == '\t' || buf[len - 1] == '\v'))
			len--;
		if (!len)
			continue;
		buf[len] = 0;


		if (strcmp(buf, "ply"))
		{
			fclose(f);
			return false;
		}
		else
			break;
	}

	while (fgets(buf, 1024, f))
	{
		int len = (int)strlen(buf);
		while (len && (buf[len - 1] == ' ' || buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == '\t' || buf[len - 1] == '\v'))
			len--;
		if (!len)
			continue;
		buf[len] = 0;

		if (strcmp(buf, "format ascii 1.0"))
		{
			fclose(f);
			return false;
		}
		else
			break;
	}

	// mesh header
	while (fgets(buf, 1024, f))
	{
		int len = (int)strlen(buf);
		while (len && (buf[len - 1] == ' ' || buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == '\t' || buf[len - 1] == '\v'))
			len--;
		if (!len)
			continue;
		buf[len] = 0;

		if (strncmp(buf, "comment", 7) == 0 && (buf[7] == 0 || buf[7] == ' ' || buf[7] == '\t' || buf[7] == '\r' || buf[7] == '\n'))
			continue;

		if (strncmp(buf, "element vertex ",15) == 0)
		{
			if (num_verts >= 0)
			{
				fclose(f);
				return false;
			}

			if (sscanf(buf+15, "%d", &num_verts) != 1 || num_verts < 0)
			{
				fclose(f);
				return false;
			}

			element = 'VERT';

			continue;
		}

		if (strncmp(buf, "element face ",13) == 0)
		{
			if (num_faces >= 0)
			{
				fclose(f);
				return false;
			}

			if (sscanf(buf+13, "%d", &num_faces) != 1 || num_faces < 0)
			{
				fclose(f);
				return false;
			}

			element = 'FACE';

			continue;
		}

		if (strncmp(buf, "property ", 9) == 0)
		{
			if (element == 'FACE')
			{
				if (strcmp(buf + 9, "list uchar uint vertex_indices") != 0)
				{
					fclose(f);
					return false;
				}

				face_props = true;
				continue;
			}
			else
			if (element == 'VERT')
			{
				static const char* match[] = 
				{
					"property float x",
					"property float y",
					"property float z",
					"property uchar red",
					"property uchar green",
					"property uchar blue",
					"property uchar alpha",
					0
				};

				if (!match[vert_props] || strcmp(buf,match[vert_props]) != 0)
				{
					fclose(f);
					return false;
				}

				vert_props++;
				continue;
			}
			else
			{
				fclose(f);
				return false;
			}
		}

		if (strcmp(buf, "end_header") == 0)
		{
			if (num_faces <= 0 || num_verts <= 0 || !face_props || vert_props != 3 && vert_props != 7)
			{
				fclose(f);
				return false;
			}
			break;
		}
		else
		{
			fclose(f);
			return false;
		}
	}

	Vert** index = (Vert**)malloc(sizeof(Vert*)*num_verts);

	// verts
	while (fgets(buf, 1024, f))
	{
		int len = (int)strlen(buf);
		while (len && (buf[len - 1] == ' ' || buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == '\t' || buf[len - 1] == '\v'))
			len--;
		if (!len)
			continue;
		buf[len] = 0;

		if (strncmp(buf, "comment", 7) == 0 && (buf[7] == 0 || buf[7] == ' ' || buf[7] == '\t' || buf[7] == '\r' || buf[7] == '\n'))
			continue;

		float x=0, y=0, z=0;
		int r=255, g=255, b=255, a=255;
		if (vert_props == 3)
		{
			if (sscanf(buf, "%f %f %f %s", &x, &y, &z, tail_str) != 3)
			{
				free(index);
				fclose(f);
				return false;
			}
		}
		else
		{
			if (sscanf(buf, "%f %f %f %d %d %d %d %s", &x, &y, &z, &r, &g, &b, &a, tail_str) != 7)
			{
				free(index);
				fclose(f);
				return false;
			}
		}

		Vert* v = (Vert*)malloc(sizeof(Vert));
		index[verts] = v;

		v->xyzw[0] = x;
		v->xyzw[1] = y;
		v->xyzw[2] = z;
		v->xyzw[3] = 1;
		v->rgba[0] = r;
		v->rgba[1] = g;
		v->rgba[2] = b;
		v->rgba[3] = a;

		if (verts && plannar)
		{
			if (plannar & 1)
			{
				if (v->xyzw[0] != head_vert->xyzw[0])
					plannar &= ~1;
			}
			if (plannar & 2)
			{
				if (v->xyzw[1] != head_vert->xyzw[1])
					plannar &= ~2;
			}
			if (plannar & 4)
			{
				if (v->xyzw[2] != head_vert->xyzw[2])
					plannar &= ~4;
			}
			if (plannar & 8)
			{
				if (v->rgba[0] != head_vert->rgba[0] ||
					v->rgba[1] != head_vert->rgba[1] ||
					v->rgba[2] != head_vert->rgba[2])
				{
					plannar &= ~8;
				}
			}
		}

		v->mesh = this;
		v->face_list = 0;
		v->line_list = 0;
		v->next = 0;
		v->prev = tail_vert;
		if (tail_vert)
			tail_vert->next = v;
		else
			head_vert = v;
		tail_vert = v;

		if (!verts)
		{
			bbox[0] = v->xyzw[0];
			bbox[1] = v->xyzw[0];
			bbox[2] = v->xyzw[1];
			bbox[3] = v->xyzw[1];
			bbox[4] = v->xyzw[2];
			bbox[5] = v->xyzw[2];
		}
		else
		{
			bbox[0] = fminf(bbox[0], v->xyzw[0]);
			bbox[1] = fmaxf(bbox[1], v->xyzw[0]);
			bbox[2] = fminf(bbox[2], v->xyzw[1]);
			bbox[3] = fmaxf(bbox[3], v->xyzw[1]);
			bbox[4] = fminf(bbox[4], v->xyzw[2]);
			bbox[5] = fmaxf(bbox[5], v->xyzw[2]);
		}

		v->sel = false;

		verts++;

		if (verts == num_verts)
			break;
	}

	// faces
	while (fgets(buf, 1024, f))
	{
		int len = (int)strlen(buf);
		while (len && (buf[len] == ' ' || buf[len] == '\r' || buf[len] == '\n' || buf[len] == '\t' || buf[len] == '\v'))
			len--;
		if (!len)
			continue;
		buf[len] = 0;

		if (strncmp(buf, "comment", 7) == 0 && (buf[7] == 0 || buf[7] == ' ' || buf[7] == '\t' || buf[7] == '\r' || buf[7] == '\n'))
			continue;

		int n, a, b, c;

		if (sscanf(buf, "%d %d %d %d %s", &n, &a, &b, &c, tail_str) != 4 || n != 3 ||
			a < 0 || a >= num_verts || b < 0 || b >= num_verts || c < 0 || c >= num_verts ||
			a == b || b == c || c == a)
		{
			free(index);
			fclose(f);
			return false;
		}

		////////////////
		Face* f = (Face*)malloc(sizeof(Face));

		int abc[3] = { a,b,c };

		f->visual = 0;
		f->mesh = this;

		f->next = 0;
		f->prev = tail_face;
		if (tail_face)
			tail_face->next = f;
		else
			head_face = f;
		tail_face = f;

		for (int i = 0; i < 3; i++)
		{
			f->abc[i] = index[abc[i]];
			f->share_next[i] = f->abc[i]->face_list;
			f->abc[i]->face_list = f;
		}

		faces++;

		if (faces == num_faces)
			break;
	}

	free(index);

	// tail
	while (fgets(buf, 1024, f))
	{
		int len = (int)strlen(buf);
		while (len && (buf[len] == ' ' || buf[len] == '\r' || buf[len] == '\n' || buf[len] == '\t' || buf[len] == '\v'))
			len--;
		if (!len)
			continue;
		buf[len] = 0;

		if (strncmp(buf, "comment", 7) == 0 && (buf[7] == 0 || buf[7] == ' ' || buf[7] == '\t' || buf[7] == '\r' || buf[7] == '\n'))
			continue;

		fclose(f);
		return false;
	}

#if 0
	// OLD .obj parser
	
	// first pass - scan all verts
	while (fgets(buf,1024,f))
    {
        // note: on every char check for # or \n or \r or 0 -> end of line

        char* p = buf;
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p=='#' || *p=='\r' || *p=='\n' || *p=='\0')
            continue;

        if (p[0] != 'v' || p[1] != ' ')
            continue;

        p += 2;

		float xyzw[4];
		float rgb[4];

		// expect 6-7 floats 
		// xyz rgb
		// xyzw rgb
		int coords = sscanf(p, "%f %f %f %f %f %f %f", xyzw + 0, xyzw + 1, xyzw + 2, xyzw + 3, rgb+0, rgb+1, rgb+2);
		if (coords < 2)
			continue;

		if (coords < 3)
		{
			xyzw[2] = 0.0f;
			xyzw[3] = 1.0f;
			rgb[0] = rgb[1] = rgb[2] = 1.0f;
		}
		else
		if (coords < 4)
		{
			xyzw[3] = 1.0f;
			rgb[0] = rgb[1] = rgb[2] = 1.0f;
		}
		else
		if (coords < 6)
			rgb[0] = rgb[1] = rgb[2] = 1.0f;
		else
		if (coords < 7)
		{
			rgb[2] = rgb[1];
			rgb[1] = rgb[0];
			rgb[0] = xyzw[3];
			xyzw[3] = 1.0f;
		}

        Vert* v = (Vert*)malloc(sizeof(Vert));
        v->xyzw[0] = xyzw[0];
        v->xyzw[1] = xyzw[1];
        v->xyzw[2] = xyzw[2];
        v->xyzw[3] = xyzw[3];
		v->rgb[0] = rgb[0];
		v->rgb[1] = rgb[1];
		v->rgb[2] = rgb[2];

        if (m->verts && plannar)
        {
            if (plannar&1)
            {
                if (v->xyzw[0] != m->head_vert->xyzw[0])
                    plannar &= ~1;
            }
            if (plannar&2)
            {
                if (v->xyzw[1] != m->head_vert->xyzw[1])
                    plannar &= ~2;
            }
            if (plannar&4)
            {
                if (v->xyzw[2] != m->head_vert->xyzw[2])
                    plannar &= ~4;
            }
			if (plannar & 8)
			{
				if (v->rgb[0] != m->head_vert->rgb[0] ||
					v->rgb[1] != m->head_vert->rgb[1] ||
					v->rgb[2] != m->head_vert->rgb[2])
				{
					plannar &= ~8;
				}
			}
        }

        v->mesh = m;
        v->face_list = 0;
        v->line_list = 0;
        v->next = 0;
        v->prev = m->tail_vert;
        if (m->tail_vert)
            m->tail_vert->next = v;
        else
            m->head_vert = v;
        m->tail_vert = v;

        if (!m->verts)
        {
            m->bbox[0] = v->xyzw[0];
            m->bbox[1] = v->xyzw[0];
            m->bbox[2] = v->xyzw[1];
            m->bbox[3] = v->xyzw[1];
            m->bbox[4] = v->xyzw[2];
            m->bbox[5] = v->xyzw[2];
        }
        else
        {
            m->bbox[0] = fminf( m->bbox[0] , v->xyzw[0] );
            m->bbox[1] = fmaxf( m->bbox[1] , v->xyzw[0] );
            m->bbox[2] = fminf( m->bbox[2] , v->xyzw[1] );
            m->bbox[3] = fmaxf( m->bbox[3] , v->xyzw[1] );
            m->bbox[4] = fminf( m->bbox[4] , v->xyzw[2] );
            m->bbox[5] = fmaxf( m->bbox[5] , v->xyzw[2] );
        }

        v->sel = false;
        m->verts++;
    }

    if (m->verts)
    {
        Vert** index = (Vert**)malloc(sizeof(Vert*)*m->verts);
        Vert* v = m->head_vert;
        for (int i=0; i<m->verts; i++)
        {
            index[i] = v;
            v = v->next;
        }

        fseek(f,0,SEEK_SET);

        int verts = 0;

        while (fgets(buf,1024,f))
        {
            char* p = buf;
            while (*p == ' ' || *p == '\t')
                p++;

            if (*p=='#' || *p=='\r' || *p=='\n' || *p=='\0')
                continue;

            if (p[1] != ' ')
                continue;

            int cmd = p[0];
            if (cmd == 'f' || cmd == 'l')
            {
                p += 2;

                // we assume it is 'f' is fan 'l" is linestrip

                int num = 0;
                int abc[3];
                char* end;

                while (abc[num]=(int)strtol(p,&end,0))
                {
                    if (abc[num] > 0)
                        abc[num] -= 1;
                    else
                        abc[num] += verts;

                    if (abc[num] >= 0 && abc[num] < m->verts)
                    {
                        if (cmd == 'f')
                        {
                            if (num<2)
                                num++;
                            else
                            {
                                // add face
                                Face* f = (Face*)malloc(sizeof(Face));

                                f->visual = 0;
                                f->mesh = m;

                                f->next = 0;
                                f->prev = m->tail_face;
                                if (m->tail_face)
                                    m->tail_face->next = f;
                                else
                                    m->head_face = f;
                                m->tail_face = f;

                                for (int i=0; i<3; i++)
                                {
                                    f->abc[i] = index[abc[i]];
                                    f->share_next[i] = f->abc[i]->face_list;
                                    f->abc[i]->face_list = f;
                                }

                                m->faces++;

                                abc[1] = abc[2];
                            }
                        }
                        else
                        if (cmd == 'l')
                        {
                            if (num<1)
                                num++;
                            else
                            {
                                // add line
                                Line* l = (Line*)malloc(sizeof(Line));

                                l->visual = 0;
                                l->mesh = m;

                                l->next = 0;
                                l->prev = m->tail_line;
                                if (m->tail_line)
                                    m->tail_line->next = l;
                                else
                                    m->head_line = l;
                                m->tail_line = l;

                                for (int i=0; i<2; i++)
                                {
                                    l->ab[i] = index[abc[i]];
                                    l->share_next[i] = l->ab[i]->line_list;
                                    l->ab[i]->line_list = l;
                                }

                                m->lines++;

                                abc[0] = abc[1];
                            }
                        }
                        
                    }

                    p = end;

                    // looking for space,digit
                    while (*p!='#' && *p!='\r' && *p!='\n' && *p!='\0')
                    {
                        if ((p[0]==' ' || p[0]=='\t') && (p[1]=='-' || p[1]=='+' || p[1]>='0' && p[1]<='9'))
                        {
                            end = 0;
                            break;
                        }
                        p++;
                    }

                    if (end)
                        break;

                    p++;
                }
            }
            else
            if (cmd == 'v')
            {
                p += 2;
                // expect 2-4 floats, ignore 4th if present
                float xyzw[4];
                int coords = sscanf(p,"%f %f %f %f", xyzw+0, xyzw+1, xyzw+2, xyzw+3);
                if (coords<2)
                    continue;

                // needed for relative indices
                verts++;
            }
        }

        free(index);
    }
	#endif // OLD .obj parser

    fclose(f);
    return true;
}

Mesh* World::LoadMesh(const char* path, const char* name)
{
    Mesh* m = AddMesh(name ? name : path);

    if (!m->Update(path))
    {
        DeleteMesh(m);
        return 0;
    }

    return m;
}

World* CreateWorld()
{
    World* w = (World*)malloc(sizeof(World));

    w->meshes = 0;
    w->head_mesh = 0;
    w->tail_mesh = 0;
    w->insts = 0;
    w->head_inst = 0;
    w->tail_inst = 0;
    w->editable = 0;
    w->root = 0;

    return w;
}

void DeleteWorld(World* w)
{
    if (!w)
        return;

    // killing bsp brings all instances to world list
	if (w->root)
	{
		w->DeleteBSP(w->root);
		w->root = 0;
	}

    // killing all meshes should kill all insts as well
    while (w->meshes)
        w->DelMesh(w->head_mesh);

	free(w);
}

Mesh* LoadMesh(World* w, const char* path, const char* name)
{
    if (!w)
        return 0;
    return w->LoadMesh(path, name);
}

bool UpdateMesh(Mesh* m, const char* path)
{
    return m->Update(path);
}

void DeleteMesh(Mesh* m)
{
    if (!m)
        return;
    m->world->DelMesh(m);
}

Inst* CreateInst(Mesh* m, int flags, const double tm[16], const char* name)
{
    if (!m)
        return 0;
    return m->world->AddInst(m,flags,tm,name);
}

void DeleteInst(Inst* i)
{
    if (!i)
        return;
    i->mesh->world->DelInst(i);
}

void QueryWorld(World* w, int planes, double plane[][4], void (*cb)(Mesh* m, const double tm[16], void* cookie), void* cookie)
{
    if (!w)
        return;
    w->Query(planes,plane,cb,cookie);
}

void QueryWorldBSP(World* w, int planes, double plane[][4], void (*cb)(int level, const float bbox[6], void* cookie), void* cookie)
{
    if (!w || !w->root)
        return;
    World::QueryBSP(1, w->root,planes,plane,cb,cookie);
}


Mesh* GetFirstMesh(World* w)
{
    if (!w)
        return 0;
    return w->head_mesh;
}

Mesh* GetLastMesh(World* w)
{
    if (!w)
        return 0;
    return w->tail_mesh;
}

Mesh* GetPrevMesh(Mesh* m)
{
    if (!m)
        return 0;
    return m->prev;
}

Mesh* GetNextMesh(Mesh* m)
{
    if (!m)
        return 0;
    return m->next;
}

World* GetMeshWorld(Mesh* m)
{
	return m->world;
}

int GetMeshName(Mesh* m, char* buf, int size)
{
    if (!m)
    {
        if (buf && size>0)
            *buf=0;
        return 0;
    }

    int len = (int)strlen(m->name);

    if (buf && size>0)
        strncpy(buf,m->name,size);

    return len+1;
}

void GetMeshBBox(Mesh* m, float bbox[6])
{
    if (!m || !bbox)
        return;

    bbox[0] = m->bbox[0];
    bbox[1] = m->bbox[1];
    bbox[2] = m->bbox[2];
    bbox[3] = m->bbox[3];
    bbox[4] = m->bbox[4];
    bbox[5] = m->bbox[5];
}

void QueryMesh(Mesh* m, void (*cb)(float coords[9], uint8_t colors[12], uint32_t visual, void* cookie), void* cookie)
{
    if (!m || !cb)
        return;

    float coords[9];
	uint8_t colors[12];

    Face* f = m->head_face;
    while (f)
    {
        coords[0] = f->abc[0]->xyzw[0];
        coords[1] = f->abc[0]->xyzw[1];
        coords[2] = f->abc[0]->xyzw[2];
		colors[0] = f->abc[0]->rgba[0];
		colors[1] = f->abc[0]->rgba[1];
		colors[2] = f->abc[0]->rgba[2];
		colors[3] = f->abc[0]->rgba[3];

        coords[3] = f->abc[1]->xyzw[0];
        coords[4] = f->abc[1]->xyzw[1];
        coords[5] = f->abc[1]->xyzw[2];
		colors[4] = f->abc[1]->rgba[0];
		colors[5] = f->abc[1]->rgba[1];
		colors[6] = f->abc[1]->rgba[2];
		colors[7] = f->abc[1]->rgba[3];

        coords[6] = f->abc[2]->xyzw[0];
        coords[7] = f->abc[2]->xyzw[1];
        coords[8] = f->abc[2]->xyzw[2];
		colors[8] = f->abc[2]->rgba[0];
		colors[9] = f->abc[2]->rgba[1];
		colors[10] = f->abc[2]->rgba[2];
		colors[11] = f->abc[2]->rgba[3];

        cb(coords, colors, f->visual, cookie);

        f=f->next;
    }
}

void* GetMeshCookie(Mesh* m)
{
    if (!m)
        return 0;
    return m->cookie;
}

void  SetMeshCookie(Mesh* m, void* cookie)
{
    if (m)
        m->cookie = cookie;
}

void RebuildWorld(World* w, bool boxes)
{
    if (w)
        w->Rebuild(boxes);
}

static void SaveInst(Inst* i, FILE* f)
{
    int mesh_id_len = i->mesh && i->mesh->name ? (int)strlen(i->mesh->name) : 0;

    fwrite(&mesh_id_len, 1,4, f);
    if (mesh_id_len)
        fwrite(i->mesh->name, 1,mesh_id_len, f);

    int inst_name_len = i->name ? (int)strlen(i->name) : 0;

    fwrite(&inst_name_len, 1,4, f);
    if (inst_name_len)
        fwrite(i->name, 1,inst_name_len, f);

    fwrite(i->tm, 1,16*8, f);
    fwrite(&i->flags, 1,4, f);
}

static void SaveQueryBSP(BSP* bsp, FILE* f) 
{
    if (bsp->type == BSP::BSP_TYPE_LEAF)
    {
        Inst* i = ((BSP_Leaf*)bsp)->head;
        while (i)
        {
            SaveInst(i,f);
            i=i->next;
        }
    }
    else
    if (bsp->type == BSP::BSP_TYPE_INST)
    {
        Inst* i = (Inst*)bsp;
        SaveInst(i,f);
    }
    else
    if (bsp->type == BSP::BSP_TYPE_NODE)
    {
        bsp_nodes++;
        BSP_Node* n = (BSP_Node*)bsp;
        if (n->bsp_child[0])
            SaveQueryBSP(n->bsp_child[0],f);
        if (n->bsp_child[1])
            SaveQueryBSP(n->bsp_child[1],f);
    }
    else
    if (bsp->type == BSP::BSP_TYPE_NODE_SHARE)
    {
        bsp_nodes++;
        BSP_NodeShare* s = (BSP_NodeShare*)bsp;
        if (s->bsp_child[0])
            SaveQueryBSP(s->bsp_child[0],f);
        if (s->bsp_child[1])
            SaveQueryBSP(s->bsp_child[1],f);
        Inst* i = s->head;
        while (i)
        {
            SaveInst(i,f);
            i=i->next;
        }                
    }
    else
    {
        assert(0);
    }
}

void SaveWorld(World* w, FILE* f)
{
    /*
    int num_of_instances;
    {
        int mesh_id_len;
        char mesh_id[mesh_id_len];
        int inst_name_len;
        char inst_name[inst_name_len];
        double tm[16];
        int flags;
    } [num_of_instances];
    */

    int num_of_instances = w->insts;
    fwrite(&num_of_instances,1,4,f);

    // non bsp first
    Inst* i = w->head_inst;
    while (i)
    {
        SaveInst(i,f);
        i=i->next;
    }

    // then bsp ones
	if (w->root)
		SaveQueryBSP(w->root,f);
}

World* LoadWorld(FILE* f)
{
    // load instances, 
    // create empty meshes if mesh-id is used for the first time
    // all subsequent instances should point to that mesh (share!)

    // after loading, asciiid will reload mesh files from ./obj dir
    // then it is responsible to match (by id) & use our empty meshes!

    World* w = CreateWorld();

    int num_of_instances = 0;
    if (1!=fread(&num_of_instances,4,1,f))
    {
        DeleteWorld(w);
        return 0;
    }
    
    for (int i=0; i<num_of_instances; i++)
    {
        int mesh_id_len = 0;
        if (1!=fread(&mesh_id_len, 4,1, f))
        {
            DeleteWorld(w);
            return 0;
        }

        char mesh_id[256]="";
        if (mesh_id_len)
            if (1!=fread(mesh_id, mesh_id_len,1, f))
            {
                DeleteWorld(w);
                return 0;
            }
        mesh_id[mesh_id_len] = 0;

        int inst_name_len = 0;
        if (1!=fread(&inst_name_len, 4,1, f))
        {
            DeleteWorld(w);
            return 0;
        }

        char inst_name[256]="";
        if (inst_name_len)
            if (1!=fread(inst_name, inst_name_len,1, f))
            {
                DeleteWorld(w);
                return 0;
            }
        inst_name[inst_name_len] = 0;

        double tm[16] = {0};
        if (1!=fread(tm, 16*8,1, f))
        {
            DeleteWorld(w);
            return 0;
        }

        int flags = 0;
        if (1!=fread(&flags, 4,1, f))
        {
            DeleteWorld(w);
            return 0;
        }

        // mesh id lookup
        Mesh* m = w->head_mesh;
        while (m && strcmp(m->name,mesh_id))
            m = m->next;

        if (!m)
            m = w->AddMesh(mesh_id);

        Inst* inst = CreateInst(m,flags,tm,inst_name);
    }

    return w;
}


Inst* HitWorld(World* w, double p[3], double v[3], double ret[3], double nrm[3])
{
    return w->HitWorld(p,v,ret,nrm);
}

Mesh* GetInstMesh(Inst* i)
{
	return i->mesh;
}

int GetInstFlags(Inst* i)
{
	return i->flags;
}

void GetInstTM(Inst* i, double tm[16])
{
	memcpy(tm, i->tm, sizeof(double[16]));
}
