SELECT n_name, SUM(l_extendedprice * (1 - l_discount)) as revenue FROM customer, orders, lineitem, supplier, nation, region WHERE c_custkey = o_custkey AND l_orderkey = o_orderkey AND l_suppkey = s_suppkey AND c_nationkey = s_nationkey AND s_nationkey = n_nationkey AND n_regionkey = r_regionkey AND r_name = 'AMERICA' AND o_orderdate BETWEEN '1994-01-01' AND '1995-01-00' GROUP BY n_name ORDER BY revenue DESC;
SELECT sum(l_extendedprice*l_discount) AS REVENUE FROM lineitem WHERE l_shipdate BETWEEN '1994-01-01' AND '1995-01-00' AND l_discount BETWEEN .06 - 0.01 AND .06 + 0.01001 AND l_quantity < 24;
SELECT c_custkey, c_name, SUM(l_extendedprice * (1 - l_discount)) as revenue, c_acctbal, n_name, c_address, c_phone, c_comment FROM customer, orders, lineitem, nation WHERE c_custkey = o_custkey AND l_orderkey = o_orderkey AND o_orderdate BETWEEN '1993-10-01' AND '1994-01-00' AND l_returnflag = 'R' AND c_nationkey = n_nationkey GROUP BY c_custkey, c_name, c_acctbal, c_phone, n_name, c_address, c_comment ORDER BY revenue DESC;
SELECT l_shipmode, SUM(case when o_orderpriority ='1-URGENT' or o_orderpriority ='2-HIGH' then 1 else 0 end) as high_line_count, SUM(case when o_orderpriority <> '1-URGENT' AND o_orderpriority <> '2-HIGH' then 1 else 0 end) as low_line_count FROM orders, lineitem WHERE o_orderkey = l_orderkey AND l_shipmode IN ('MAIL','SHIP') AND l_commitdate < l_receiptdate AND l_shipdate < l_commitdate AND l_receiptdate BETWEEN '1994-01-01' AND '1995-01-00' GROUP BY l_shipmode ORDER BY l_shipmode;
SELECT 100.00 * SUM(case when p_type like 'PROMO%' then l_extendedprice*(1-l_discount) else 0 end) / SUM(l_extendedprice * (1 - l_discount)) as promo_revenue FROM lineitem, part WHERE l_partkey = p_partkey AND l_shipdate BETWEEN '1995-09-01' AND '1995-10-00';
SELECT SUM(l_extendedprice * (1 - l_discount) ) as revenue FROM lineitem, part WHERE (( p_partkey = l_partkey AND p_brand = 'Brand#12' AND p_container in ( 'SM CASE', 'SM BOX', 'SM PACK', 'SM PKG') AND l_quantity BETWEEN 1 AND 1 + 10 AND p_size between 1 AND 5 AND l_shipmode in ('AIR', 'AIR REG') AND l_shipinstruct = 'DELIVER IN PERSON') or (p_partkey = l_partkey AND p_brand = 'Brand#23' AND p_container in ('MED BAG', 'MED BOX', 'MED PKG', 'MED PACK') AND l_quantity BETWEEN 10 AND 10 + 10 AND p_size between 1 AND 10 AND l_shipmode in ('AIR', 'AIR REG') AND l_shipinstruct = 'DELIVER IN PERSON') or (p_partkey = l_partkey AND p_brand = 'Brand#34' AND p_container in ('LG CASE', 'LG BOX', 'LG PACK', 'LG PKG') AND l_quantity BETWEEN 20 AND 20 + 10 AND p_size between 1 AND 15 AND l_shipmode in ('AIR', 'AIR REG') AND l_shipinstruct = 'DELIVER IN PERSON'));
SELECT s_name, s_address FROM supplier, nation WHERE s_suppkey in (SELECT ps_suppkey FROM partsupp WHERE ps_partkey in (SELECT p_partkey FROM part WHERE p_name like 'forest%') AND ps_availqty > (SELECT 0.5 * SUM(l_quantity) FROM lineitem WHERE l_partkey = ps_partkey AND l_suppkey = ps_suppkey AND l_shipdate BETWEEN '1994-01-01' AND '1995-01-00')) AND s_nationkey = n_nationkey AND n_name = 'CANADA' ORDER BY s_name;
