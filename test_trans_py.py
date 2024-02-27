def trans(M, N, a, b):
    for kk in range(0,4,2):
        for jj in range(0,4,2):
            for j in range(jj,jj+B_32):
                for k in range(kk,kk+B_32):
                    if (j == k):
                        b[j][j] = a[k][k]
                    else:
                        b[j][k] = a[k][j]
                
                # print(b)
    print('ans: ',b)

a = [[1,2,3,4], [5,6,7,8], [9,10,11,12], [13,14,15,16]]
b = [[0]*4 for i in range(4)]
B_32 = 2
en = 4

trans(4,4,a,b)