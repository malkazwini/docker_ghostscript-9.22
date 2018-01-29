# Dockerized Ghostscript 9.22 

## Running steps 

clone the repo 
docker build -t gs:latest . 
docker run -d gs:latest 

docker exec -it <CONTAINER_ID> bash 
