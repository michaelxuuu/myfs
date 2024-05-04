import random
import string

def generate_random_word(length):
    letters = string.ascii_lowercase
    return ''.join(random.choice(letters) for _ in range(length))

def generate_random_file(file_name, target_size):
    with open(file_name, 'w') as file:
        while file.tell() < target_size:
            word_length = random.randint(3, 10)  # Random word length between 3 and 10 characters
            word = generate_random_word(word_length)
            file.write(word + '\n')

if __name__ == "__main__":
    file_name = "random.txt"  # You can change the file name here
    target_size = 2000  # Target size of the file in bytes
    generate_random_file(file_name, target_size)
