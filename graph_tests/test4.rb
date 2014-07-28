# Deleting edges

require 'redis'

redis = Redis.new

describe 'Minimum spanning tree' do

  it 'should correctly remove edges from undirected graphs' do
    redis.flushdb
    redis.gvertex 'graph1', 'a', 'b', 'c', 'd'
    redis.gedge 'graph1', 'a', 'b', 1
    redis.gedge 'graph1', 'b', 'c', 1
    redis.gedge 'graph1', 'c', 'd', 1
    redis.gedge 'graph1', 'a', 'd', 2
    redis.gedgerem 'graph1', 'a', 'd'
    path = redis.gshortestpath 'graph1', 'a', 'd'
    path.last.should eq '3'
    path = redis.gshortestpath 'graph1', 'd', 'a'
    path.last.should eq '3'
  end

end

